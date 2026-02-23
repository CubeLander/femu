# MMU / Trap Deep Dive

本文档对应 `Package B`，基于当前实现说明 SV32 地址翻译、权限检查、陷入委派与 `cause/tval` 更新规则。

主要对应源码：
- `src/rv32emu_virt_trap.c`
- `src/rv32emu_csr.c`
- `include/rv32emu.h`

## 1. 地址翻译与权限检查（SV32）

入口函数是 `rv32emu_translate()`（`src/rv32emu_virt_trap.c:159`）。

### 1.1 控制流图

```text
virt_read/virt_write/fetch
  -> rv32emu_translate(vaddr, access)
     -> read satp + mstatus
     -> 计算 effective_priv（MPRV+MPP 仅影响 M 态数据访问）
     -> if satp.MODE != Sv32 or effective_priv == M:
          直接 paddr=vaddr
        else:
          两级页表遍历 (level=1 -> 0)
            - 读 PTE
            - 校验 V/R/W 组合合法性
            - 非叶子则降级到下一层
            - 叶子则做 U/SUM/MXR + R/W/X 权限判断
            - 必要时回写 A/D 位
            - 组装物理地址并返回
     -> 任一步失败：raise page fault（tval=vaddr）
  -> phys_read/phys_write
     -> 失败：raise access fault（tval=vaddr）
```

### 1.2 关键规则（与代码逐条对齐）

- `satp.MODE` 只检查 bit31（`SATP_MODE_SV32`），页表根 `PPN` 用 `0x003fffff` 掩码（`src/rv32emu_virt_trap.c:5-6`）。
- 若当前为 M 态且访问不是取指，且 `MSTATUS.MPRV=1`，则按 `MSTATUS.MPP` 作为 `effective_priv` 做地址翻译（`src/rv32emu_virt_trap.c:177-181`）。
- 关闭翻译条件：`satp.MODE != Sv32` 或 `effective_priv == M`，直接 VA=PA（`src/rv32emu_virt_trap.c:183-186`）。
- PTE 非法条件：`V=0` 或 `R=0且W=1`，立即 page fault（`src/rv32emu_virt_trap.c:221-223`）。
- 叶子判定：`leaf = R || X`（`src/rv32emu_virt_trap.c:219`）。
- U/S 权限：
  - U 态访问 `U=0` 页 -> page fault（`src/rv32emu_virt_trap.c:235-237`）。
  - S 态访问 `U=1` 页：取指总是 fault；读写仅在 `SUM=1` 时允许（`src/rv32emu_virt_trap.c:240-246`）。
- 访问类型检查：
  - Fetch 需要 `X=1`
  - Load 需要 `R=1`，或 `MXR=1 && X=1`
  - Store 需要 `W=1`
  （`src/rv32emu_virt_trap.c:249-257`）
- A/D 位处理：访问时若 `A=0` 或（Store 且 `D=0`），会尝试回写置位；回写失败也按 page fault（`src/rv32emu_virt_trap.c:261-268`）。
- 超页对齐检查：L1 叶子要求 `PPN0==0`，否则 page fault（`src/rv32emu_virt_trap.c:274-276`）。

### 1.3 虚拟访存层

- `rv32emu_virt_read()` / `rv32emu_virt_write()` 先翻译、再物理访问（`src/rv32emu_virt_trap.c:295-337`）。
- 物理访问失败会转成 access fault：
  - 取指 -> `INST_ACCESS_FAULT`
  - 读 -> `LOAD_ACCESS_FAULT`
  - 写 -> `STORE_ACCESS_FAULT`
  （`src/rv32emu_virt_trap.c:67-77`, `src/rv32emu_virt_trap.c:303-307`, `src/rv32emu_virt_trap.c:321-325`）

## 2. Trap 委派与 `cause/tval` 更新

核心逻辑在 `rv32emu_take_trap()`（`src/rv32emu_virt_trap.c:80`）。

### 2.1 控制流图

```text
rv32emu_raise_exception(cause,tval)
rv32emu_check_pending_interrupt() -> take_trap(selected,0,is_interrupt=true)

rv32emu_take_trap(cause,tval,is_interrupt):
  cause_value = cause | (is_interrupt ? 0x80000000 : 0)
  deleg_mask = is_interrupt ? MIDELEG : MEDELEG
  delegated_to_s = (current_priv != M) && deleg_mask[cause_bit]

  if delegated_to_s:
    SEPC   = current_pc
    SCAUSE = cause_value
    STVAL  = tval
    SPIE <= SIE
    SIE  <= 0
    SPP  <= (prev_priv == S)
    pc   <= STVEC.base (+ 4*cause for vectored interrupt)
    priv <= S
  else:
    MEPC   = current_pc
    MCAUSE = cause_value
    MTVAL  = tval
    MPIE <= MIE
    MIE  <= 0
    MPP  <= prev_priv
    pc   <= MTVEC.base (+ 4*cause for vectored interrupt)
    priv <= M
```

### 2.2 更新细节

- 中断会给 `mcause/scause` 置高位（bit31）（`src/rv32emu_virt_trap.c:88-90`）。
- 只在“当前不在 M 态”时才允许委派到 S 态（`src/rv32emu_virt_trap.c:92-95`）。
- S 委派路径更新 `SEPC/SCAUSE/STVAL`，并按 `SIE->SPIE`、`SPP` 保存先前状态（`src/rv32emu_virt_trap.c:101-116`）。
- M 路径更新 `MEPC/MCAUSE/MTVAL`，并按 `MIE->MPIE`、`MPP` 保存先前状态（`src/rv32emu_virt_trap.c:134-145`）。
- `tvec` 向量模式仅对“中断且 mode==1”生效，异常始终走 base（`src/rv32emu_virt_trap.c:118-121`, `src/rv32emu_virt_trap.c:147-150`）。
- 跳转目标为 0 时会把 `running=false`（`src/rv32emu_virt_trap.c:126`, `src/rv32emu_virt_trap.c:155`）。

## 3. 实现中使用的 cause code

定义位置：`include/rv32emu.h:46-70`。

### 3.1 Exception codes（当前实现会触发）

| Code | 名称 | 主要触发路径 |
|---|---|---|
| 0 | `RV32EMU_EXC_INST_MISALIGNED` | PC 非对齐取指（`src/rv32emu_cpu_exec.c:1113`） |
| 1 | `RV32EMU_EXC_INST_ACCESS_FAULT` | 取指物理访存失败（`src/rv32emu_virt_trap.c:71-73`） |
| 2 | `RV32EMU_EXC_ILLEGAL_INST` | 非法/不支持指令、非法 CSR/特权条件等（`src/rv32emu_cpu_exec.c` 多处） |
| 3 | `RV32EMU_EXC_BREAKPOINT` | `ebreak`（`src/rv32emu_cpu_exec.c:1013`） |
| 4 | `RV32EMU_EXC_LOAD_MISALIGNED` | 例如 `amo*` 地址非 4 字节对齐（`src/rv32emu_cpu_exec.c:364`） |
| 5 | `RV32EMU_EXC_LOAD_ACCESS_FAULT` | 数据读物理访存失败（`src/rv32emu_virt_trap.c:69`, `src/rv32emu_virt_trap.c:303-307`） |
| 7 | `RV32EMU_EXC_STORE_ACCESS_FAULT` | 数据写物理访存失败（`src/rv32emu_virt_trap.c:74`, `src/rv32emu_virt_trap.c:321-325`） |
| 8 | `RV32EMU_EXC_ECALL_U` | U 态 `ecall`（`src/rv32emu_cpu_exec.c:1004`） |
| 9 | `RV32EMU_EXC_ECALL_S` | S 态 `ecall`（`src/rv32emu_cpu_exec.c:1002`） |
| 11 | `RV32EMU_EXC_ECALL_M` | M 态 `ecall`（`src/rv32emu_cpu_exec.c:1000`） |
| 12 | `RV32EMU_EXC_INST_PAGE_FAULT` | 取指页错误（`src/rv32emu_virt_trap.c:58-60`） |
| 13 | `RV32EMU_EXC_LOAD_PAGE_FAULT` | 读页错误（`src/rv32emu_virt_trap.c:56`, `src/rv32emu_virt_trap.c:210-223`） |
| 15 | `RV32EMU_EXC_STORE_PAGE_FAULT` | 写页错误（`src/rv32emu_virt_trap.c:60-62`） |

补充：`RV32EMU_EXC_STORE_MISALIGNED`（code 6）在头文件中定义了，但当前源码中没有直接触发路径。

### 3.2 Interrupt codes（当前实现检查并可受理）

`rv32emu_check_pending_interrupt()` 使用固定优先级：
`MEIP > MSIP > MTIP > SEIP > SSIP > STIP`（`src/rv32emu_virt_trap.c:361-362`）。

| Code | 名称 | 典型置位来源 |
|---|---|---|
| 1 | `RV32EMU_IRQ_SSIP` | SBI shim 相关软件中断注入（如 `src/rv32emu_stubs.c`） |
| 3 | `RV32EMU_IRQ_MSIP` | CLINT `msip`（`src/rv32emu_mmio_clint_timer.c:131-133`） |
| 5 | `RV32EMU_IRQ_STIP` | 启用 SBI shim 时的 supervisor timer（`src/rv32emu_mmio_clint_timer.c:25-27`） |
| 7 | `RV32EMU_IRQ_MTIP` | CLINT machine timer（`src/rv32emu_mmio_clint_timer.c:34-36`） |
| 9 | `RV32EMU_IRQ_SEIP` | PLIC S-context 外部中断（`src/rv32emu_mmio_uart_plic.c:69-71`） |
| 11 | `RV32EMU_IRQ_MEIP` | PLIC M-context 外部中断（`src/rv32emu_mmio_uart_plic.c:63-65`） |

## 4. CSR 视角下的委派与可见性

- `SSTATUS`/`SIE`/`SIP` 是 `MSTATUS`/`MIE`/`MIP` 的掩码视图，不是独立存储（`src/rv32emu_csr.c:3-6`, `src/rv32emu_csr.c:62-69`, `src/rv32emu_csr.c:108-121`）。
- `MEDELEG`/`MIDELEG`、`SEPC/SCAUSE/STVAL`、`MEPC/MCAUSE/MTVAL` 都在 CSR 表中实现，可被 CSR 指令读写（`src/rv32emu_csr.c:7-44`, `src/rv32emu_csr.c:130-135`）。

## 5. 与完整 Privileged Spec 的简化差异

以下是当前实现中可观察到的简化：

1. 仅支持 `satp.MODE=Sv32` 与 `Bare` 两种分支判断；未实现 ASID 语义与 TLB（`src/rv32emu_virt_trap.c:5-6`, `src/rv32emu_virt_trap.c:183-193`）。
2. A/D 位采用“软件步进时自动置位并回写 PTE”的行为；若回写失败直接 page fault（`src/rv32emu_virt_trap.c:261-268`）。
3. `STORE_MISALIGNED` 已定义但当前未见触发路径（定义在 `include/rv32emu.h:53`）。
4. 中断仲裁使用固定优先级数组，仅覆盖 6 个标准中断号（`src/rv32emu_virt_trap.c:361-362`）。
5. trap 入口地址若为 0 会令 hart 停止运行（`running=false`），这是实现层行为，不是架构规范要求（`src/rv32emu_virt_trap.c:126`, `src/rv32emu_virt_trap.c:155`）。

## Assumptions

1. “used by implementation”按“当前源码中可触发或被仲裁路径引用”判定，而不是仅看头文件定义。
2. 中断置位来源可引用平台文件（`mmio_*`/`stubs`），即使 Package B 的 primary source 主要聚焦 trap/CSR 核心路径。

## 未决问题

1. `RV32EMU_EXC_STORE_MISALIGNED` 是否计划补齐触发路径（如普通 store/AMO 的统一对齐异常语义）？
2. 是否需要在后续版本引入 TLB/ASID 与 `sfence.vma` 的可观测行为（当前 `sfence.vma` 仅做特权检查）？
3. trap 目标向量为 0 即停机（`running=false`）是否应改为更显式的 fatal trap 处理策略？
