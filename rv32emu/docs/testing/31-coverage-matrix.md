# Coverage Matrix (Package D)

本矩阵仅基于以下当前测试文件：

- `tests/test_platform.c`
- `tests/test_run.c`
- `tests/test_system.c`
- `tests/test_loader.c`

## 1. Feature Claims to Evidence

| Claim ID | Feature claim | Test evidence | Implementation path |
|---|---|---|---|
| FC-PLAT-01 | 物理内存读写与虚拟访问基础路径可用 | `tests/test_platform.c:17` | `src/memory/rv32emu_memory_mmio.c`, `src/cpu/rv32emu_virt_trap.c` |
| FC-PLAT-02 | UART LSR/RBR 接收路径与 `uart_push_rx` 行为可见 | `tests/test_platform.c:26`, `tests/test_platform.c:30` | `src/memory/rv32emu_mmio_uart_plic.c` |
| FC-PLAT-03 | UART 中断经 PLIC claim/complete 触发并可清除 | `tests/test_platform.c:38`, `tests/test_platform.c:43` | `src/memory/rv32emu_mmio_uart_plic.c` |
| FC-PLAT-04 | CLINT `mtimecmp`/`msip` 对 `MIP_MTIP`/`MIP_MSIP` 生效 | `tests/test_platform.c:56`, `tests/test_platform.c:66` | `src/memory/rv32emu_mmio_clint_timer.c`, `src/memory/rv32emu_memory_mmio.c` |
| FC-PLAT-05 | 多 hart 下 CLINT/PLIC 中断路由按 hart 生效 | `tests/test_platform.c:87`, `tests/test_platform.c:99` | `src/memory/rv32emu_mmio_clint_timer.c`, `src/memory/rv32emu_mmio_uart_plic.c` |
| FC-RUN-01 | 基础 RV32I 执行与 `ebreak` 停机路径可用 | `tests/test_run.c:21` | `src/cpu/rv32emu_cpu_exec.c`, `src/cpu/rv32emu_cpu_run.c` |
| FC-RUN-02 | RVC 基础执行路径（`c.addi/c.mv/c.add/c.ebreak`）可用 | `tests/test_run.c:54` | `src/cpu/rv32emu_decode.c`, `src/cpu/rv32emu_cpu_exec.c` |
| FC-RUN-03 | 多 hart round-robin 调度可在单次 `run` 中推进两 hart | `tests/test_run.c:88` | `src/cpu/rv32emu_cpu_run.c:36`, `src/cpu/rv32emu_cpu_run.c:230` |
| FC-RUN-04 | 跨 hart 写入会使 `lr/sc` reservation 失效，`sc.w` 返回失败 | `tests/test_run.c:131` | `src/cpu/rv32emu_cpu_exec.c` |
| FC-RUN-05 | JIT 整数 ALU 子集执行结果与解释器语义一致 | `tests/test_run.c:219` | `src/tb/rv32emu_tb.c`, `src/cpu/rv32emu_cpu_run.c` |
| FC-RUN-06 | JIT/chaining 路径不会突破 `max_instructions` 预算边界 | `tests/test_run.c:294` | `src/tb/rv32emu_tb.c`, `src/cpu/rv32emu_cpu_run.c` |
| FC-RUN-07 | JIT load/store helper 路径在 `sw/lw` 上与解释器一致 | `tests/test_run.c:336` | `src/tb/rv32emu_tb.c`, `src/cpu/rv32emu_cpu_run.c`, `src/cpu/rv32emu_virt_trap.c` |
| FC-RUN-08 | JIT fault 中间退出时仅提交 fault 前指令（partial retire） | `tests/test_run.c:380` | `src/tb/rv32emu_tb.c`, `src/cpu/rv32emu_cpu_run.c`, `src/cpu/rv32emu_virt_trap.c` |
| FC-RUN-09 | JIT 首条指令 fault 时返回 handled-no-retire，且不产生伪 retire | `tests/test_run.c:461` | `src/tb/rv32emu_tb.c`, `src/cpu/rv32emu_cpu_run.c` |
| FC-RUN-10 | JIT block 入口 pending interrupt 会 no-retire 退出并跳转 trap 向量 | `tests/test_run.c:502` | `src/tb/rv32emu_tb.c`, `src/cpu/rv32emu_virt_trap.c` |
| FC-RUN-11 | JIT 链式执行遇到中断时，trap/mret 后可从跨 block 边界正确恢复 | `tests/test_run.c:553` | `src/tb/rv32emu_tb.c`, `src/cpu/rv32emu_cpu_run.c`, `src/cpu/rv32emu_virt_trap.c` |
| FC-RUN-12 | JIT 跨 block fault 进入 handler 后，`mepc` 修正可恢复到 fault 后指令 | `tests/test_run.c:624` | `src/tb/rv32emu_tb.c`, `src/cpu/rv32emu_cpu_run.c`, `src/cpu/rv32emu_virt_trap.c` |
| FC-RUN-13 | JIT 到解释器的 branch side-exit 可保持控制流正确性 | `tests/test_run.c:702` | `src/tb/rv32emu_tb.c`, `src/cpu/rv32emu_cpu_run.c`, `src/cpu/rv32emu_cpu_exec.c` |
| FC-RUN-14 | JIT 链式执行中的多次 trap/mret 恢复保持寄存器与 `mepc` 一致 | `tests/test_run.c:748` | `src/tb/rv32emu_tb.c`, `src/cpu/rv32emu_cpu_run.c`, `src/cpu/rv32emu_virt_trap.c` |
| FC-RUN-15 | JIT `jal/jalr` helper 终止路径保持跳转与返回地址语义一致 | `tests/test_run.c:815` | `src/tb/rv32emu_tb.c`, `src/cpu/rv32emu_cpu_run.c` |
| FC-SYS-01 | SBI shim 开关、BASE/TIME/legacy timer/shutdown/unknown ext 行为可用 | `tests/test_system.c:76` | `src/platform/rv32emu_stubs.c:322` |
| FC-SYS-02 | SBI HSM `hart_status`/`hart_start` 与重复启动错误码生效 | `tests/test_system.c:163` | `src/platform/rv32emu_stubs.c` |
| FC-SYS-03 | Sv32 翻译会更新 A/D 位，虚拟写入落到期望物理页 | `tests/test_system.c:212` | `src/cpu/rv32emu_virt_trap.c:159` |
| FC-SYS-04 | M-mode 下 `MSTATUS.MPRV` 可切换数据访问翻译语义 | `tests/test_system.c:251` | `src/cpu/rv32emu_virt_trap.c:159` |
| FC-SYS-05 | Sv32 权限失败会设置 `MCAUSE=LOAD_PAGE_FAULT` 与 `MTVAL` | `tests/test_system.c:287` | `src/cpu/rv32emu_virt_trap.c` |
| FC-SYS-06 | `sfence.vma` 在 S 态可执行，在 U 态触发非法指令 | `tests/test_system.c:314` | `src/cpu/rv32emu_cpu_exec_system.c`, `src/cpu/rv32emu_cpu_exec.c` |
| FC-SYS-07 | M 扩展 `mul/div` 与 A 扩展 `amoadd/lr/sc` 基础路径可用 | `tests/test_system.c:351` | `src/cpu/rv32emu_cpu_exec.c` |
| FC-SYS-08 | F/D 访存与 `fmv`/`fsgnj.d` 基础路径可用 | `tests/test_system.c:391` | `src/cpu/rv32emu_cpu_exec.c` |
| FC-SYS-09 | 压缩浮点访存 `c.fld/c.fsdsp/c.fldsp/c.fsd` 可用 | `tests/test_system.c:445` | `src/cpu/rv32emu_decode.c`, `src/cpu/rv32emu_cpu_exec.c` |
| FC-SYS-10 | CSR 别名（`sstatus/sie/sip`）映射与屏蔽规则生效 | `tests/test_system.c:494` | `src/cpu/rv32emu_csr.c` |
| FC-SYS-11 | CSR 指令 `csrrw/csrrs/csrrsi/csrrci` 基础语义可用 | `tests/test_system.c:521` | `src/cpu/rv32emu_cpu_exec_system.c` |
| FC-SYS-12 | `mret/sret` 恢复特权级与中断位路径可用 | `tests/test_system.c:556`, `tests/test_system.c:592` | `src/cpu/rv32emu_cpu_exec_system.c` |
| FC-SYS-13 | 异常陷入后通过 handler 修改 `mepc` 并 `mret` 返回可用 | `tests/test_system.c:628` | `src/cpu/rv32emu_virt_trap.c`, `src/cpu/rv32emu_cpu_exec_system.c` |
| FC-SYS-14 | 机器态中断陷入/清 pending/`mret` 返回路径可用 | `tests/test_system.c:664` | `src/cpu/rv32emu_virt_trap.c`, `src/cpu/rv32emu_cpu_exec_system.c` |
| FC-SYS-15 | `mtvec` vectored mode 中断入口偏移路径可用 | `tests/test_system.c:707` | `src/cpu/rv32emu_virt_trap.c` |
| FC-SYS-16 | `mideleg` 可将定时器中断委托到 S 态并由 `sret` 返回 | `tests/test_system.c:751` | `src/cpu/rv32emu_virt_trap.c`, `src/cpu/rv32emu_cpu_exec_system.c` |
| FC-SYS-17 | `medeleg` 可将异常委托到 S 态处理 | `tests/test_system.c:794` | `src/cpu/rv32emu_virt_trap.c` |
| FC-LOAD-01 | RAW 加载可按字节写入，自动识别 RAW 时 `entry_valid=false` | `tests/test_loader.c:39` | `src/platform/rv32emu_stubs.c:386`, `src/platform/rv32emu_stubs.c:419` |
| FC-LOAD-02 | ELF32 PT_LOAD 加载、BSS 清零与自动识别 ELF entry 可用 | `tests/test_loader.c:72` | `src/platform/rv32emu_stubs.c:386`, `src/platform/rv32emu_stubs.c:470` |

## 2. Test-to-Claim Trace

| Test case | Source | Linked claims |
|---|---|---|
| `test_platform main` | `tests/test_platform.c:7` | FC-PLAT-01, FC-PLAT-02, FC-PLAT-03, FC-PLAT-04, FC-PLAT-05 |
| `test_base32` | `tests/test_run.c:21` | FC-RUN-01 |
| `test_rvc_basic` | `tests/test_run.c:54` | FC-RUN-02 |
| `test_multihart_round_robin` | `tests/test_run.c:88` | FC-RUN-03 |
| `test_multihart_lr_sc_invalidation` | `tests/test_run.c:131` | FC-RUN-04 |
| `test_jit_int_alu` | `tests/test_run.c:219` | FC-RUN-05 |
| `test_jit_budget_respected` | `tests/test_run.c:294` | FC-RUN-06 |
| `test_jit_load_store_basic` | `tests/test_run.c:336` | FC-RUN-07 |
| `test_jit_fault_partial_retire` | `tests/test_run.c:380` | FC-RUN-08 |
| `test_jit_fault_last_insn_boundary` | `tests/test_run.c:418` | FC-RUN-08 |
| `test_jit_fault_first_insn_no_retire` | `tests/test_run.c:461` | FC-RUN-09 |
| `test_jit_interrupt_no_retire` | `tests/test_run.c:502` | FC-RUN-10 |
| `test_jit_chain_interrupt_then_handler_resume` | `tests/test_run.c:553` | FC-RUN-11 |
| `test_jit_cross_block_fault_handler_entry` | `tests/test_run.c:624` | FC-RUN-12 |
| `test_jit_chain_branch_side_exit_recovery` | `tests/test_run.c:702` | FC-RUN-13 |
| `test_jit_multi_trap_resume_consistency` | `tests/test_run.c:748` | FC-RUN-14 |
| `test_jit_jal_jalr_helper_paths` | `tests/test_run.c:815` | FC-RUN-15 |
| `test_sbi_shim_handle_and_ecall` | `tests/test_system.c:76` | FC-SYS-01 |
| `test_sbi_hsm_start_status` | `tests/test_system.c:163` | FC-SYS-02 |
| `test_sv32_translate_and_ad_bits` | `tests/test_system.c:212` | FC-SYS-03 |
| `test_mprv_translate_for_m_mode_data_access` | `tests/test_system.c:251` | FC-SYS-04 |
| `test_sv32_permission_fault` | `tests/test_system.c:287` | FC-SYS-05 |
| `test_sfence_vma` | `tests/test_system.c:314` | FC-SYS-06 |
| `test_m_ext_and_amo` | `tests/test_system.c:351` | FC-SYS-07 |
| `test_fp_load_store_and_moves` | `tests/test_system.c:391` | FC-SYS-08 |
| `test_compressed_fp_load_store` | `tests/test_system.c:445` | FC-SYS-09 |
| `test_csr_alias` | `tests/test_system.c:494` | FC-SYS-10 |
| `test_csr_ops` | `tests/test_system.c:521` | FC-SYS-11 |
| `test_mret` | `tests/test_system.c:556` | FC-SYS-12 |
| `test_sret` | `tests/test_system.c:592` | FC-SYS-12 |
| `test_trap_vector_mret` | `tests/test_system.c:628` | FC-SYS-13 |
| `test_interrupt_trap_mret` | `tests/test_system.c:664` | FC-SYS-14 |
| `test_mtvec_vectored_interrupt` | `tests/test_system.c:707` | FC-SYS-15 |
| `test_mideleg_interrupt_to_s` | `tests/test_system.c:751` | FC-SYS-16 |
| `test_medeleg_exception_to_s` | `tests/test_system.c:794` | FC-SYS-17 |
| `test_load_raw_and_auto` | `tests/test_loader.c:39` | FC-LOAD-01 |
| `test_load_elf32_and_auto` | `tests/test_loader.c:72` | FC-LOAD-02 |

## 3. Coverage Snapshot

- 已覆盖核心路径：平台 MMIO（UART/CLINT/PLIC）、基础/压缩执行、多 hart 基础调度、SBI 基础、Sv32 翻译与委托、loader RAW/ELF32。
- 未在当前矩阵中声明为“已覆盖”的项目，见 `docs/testing/32-gap-analysis.md`。
