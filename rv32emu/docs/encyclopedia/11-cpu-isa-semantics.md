# CPU ISA Semantics (Code-Aligned)

关联章节：`10-cpu-execution.md`（执行分组与主流程）

- 返回上游章节：[`10-cpu-execution.md`](./10-cpu-execution.md)
- 本文只描述当前实现到源码中的语义，不补全未实现 ISA 子集。

## 1. 执行入口与 Opcode 分发

32-bit 指令统一入口：`src/rv32emu_cpu_exec.c:1048` 的 `rv32emu_exec_decoded`。

| Opcode | 组 | 分发函数 | 源码 |
|---|---|---|---|
| `0x37 0x17 0x6f 0x67 0x63` | 控制流 | `rv32emu_exec_cf_group` | `src/rv32emu_cpu_exec.c:766` |
| `0x03 0x07 0x23 0x27` | 访存/FP访存 | `rv32emu_exec_mem_group` | `src/rv32emu_cpu_exec.c:838` |
| `0x13 0x33` | 整数 ALU / M 扩展 | `rv32emu_exec_int_group` | `src/rv32emu_cpu_exec.c:870` |
| `0x0f 0x2f 0x53 0x73` | Fence/AMO/FP/System | `rv32emu_exec_misc_group` | `src/rv32emu_cpu_exec.c:976` |

压缩指令入口：`rv32emu_exec_one` 在 16-bit 路径调用 `rv32emu_exec_compressed`（`src/rv32emu_cpu_exec.c:1106`, `src/rv32emu_cpu_exec.c:476`）。

## 2. RV32I 语义表

### 2.1 控制流与上位立即数

| 指令 | 编码条件 | 语义（简写） | 实现路径 |
|---|---|---|---|
| `lui` | `opcode=0x37` | `x[rd] = imm_u` | `src/rv32emu_cpu_exec.c:769` |
| `auipc` | `opcode=0x17` | `x[rd] = pc + imm_u` | `src/rv32emu_cpu_exec.c:772` |
| `jal` | `opcode=0x6f` | `x[rd]=pc+4; pc=pc+imm_j` | `src/rv32emu_cpu_exec.c:775` |
| `jalr` | `opcode=0x67, funct3=0` | `t=pc+4; pc=(x[rs1]+imm_i)&~1; x[rd]=t` | `src/rv32emu_cpu_exec.c:779` |
| `beq` | `opcode=0x63, funct3=0` | 相等则跳转 `pc+imm_b` | `src/rv32emu_cpu_exec.c:791` |
| `bne` | `opcode=0x63, funct3=1` | 不等则跳转 | `src/rv32emu_cpu_exec.c:796` |
| `blt` | `opcode=0x63, funct3=4` | 有符号小于则跳转 | `src/rv32emu_cpu_exec.c:801` |
| `bge` | `opcode=0x63, funct3=5` | 有符号大于等于则跳转 | `src/rv32emu_cpu_exec.c:806` |
| `bltu` | `opcode=0x63, funct3=6` | 无符号小于则跳转 | `src/rv32emu_cpu_exec.c:811` |
| `bgeu` | `opcode=0x63, funct3=7` | 无符号大于等于则跳转 | `src/rv32emu_cpu_exec.c:816` |

### 2.2 整数访存

| 指令 | 编码条件 | 语义（简写） | 实现路径 |
|---|---|---|---|
| `lb` | `opcode=0x03, funct3=0` | 读 8-bit 并符号扩展 | `src/rv32emu_cpu_exec.c:59` |
| `lh` | `opcode=0x03, funct3=1` | 读 16-bit 并符号扩展（非对齐按字节拼接） | `src/rv32emu_cpu_exec.c:65` |
| `lw` | `opcode=0x03, funct3=2` | 读 32-bit（非对齐按字节拼接） | `src/rv32emu_cpu_exec.c:79` |
| `lbu` | `opcode=0x03, funct3=4` | 读 8-bit 零扩展 | `src/rv32emu_cpu_exec.c:92` |
| `lhu` | `opcode=0x03, funct3=5` | 读 16-bit 零扩展（非对齐按字节拼接） | `src/rv32emu_cpu_exec.c:98` |
| `sb` | `opcode=0x23, funct3=0` | 写 8-bit | `src/rv32emu_cpu_exec.c:123` |
| `sh` | `opcode=0x23, funct3=1` | 写 16-bit（非对齐拆成 2 次 byte 写） | `src/rv32emu_cpu_exec.c:126` |
| `sw` | `opcode=0x23, funct3=2` | 写 32-bit（非对齐拆成 4 次 byte 写） | `src/rv32emu_cpu_exec.c:134` |

### 2.3 OP-IMM

| 指令 | 编码条件 | 语义（简写） | 实现路径 |
|---|---|---|---|
| `addi` | `opcode=0x13, funct3=0` | `x[rd]=x[rs1]+imm_i` | `src/rv32emu_cpu_exec.c:875` |
| `slti` | `opcode=0x13, funct3=2` | 有符号比较置位 | `src/rv32emu_cpu_exec.c:878` |
| `sltiu` | `opcode=0x13, funct3=3` | 无符号比较置位 | `src/rv32emu_cpu_exec.c:881` |
| `xori` | `opcode=0x13, funct3=4` | 异或立即数 | `src/rv32emu_cpu_exec.c:884` |
| `ori` | `opcode=0x13, funct3=6` | 或立即数 | `src/rv32emu_cpu_exec.c:887` |
| `andi` | `opcode=0x13, funct3=7` | 与立即数 | `src/rv32emu_cpu_exec.c:890` |
| `slli` | `opcode=0x13, funct3=1` | 逻辑左移（当前实现使用 `decoded->rs2` 作为 shamt） | `src/rv32emu_cpu_exec.c:893` |
| `srli` | `opcode=0x13, funct3=5, funct7=0x00` | 逻辑右移 | `src/rv32emu_cpu_exec.c:896` |
| `srai` | `opcode=0x13, funct3=5, funct7=0x20` | 算术右移 | `src/rv32emu_cpu_exec.c:901` |

### 2.4 OP（寄存器-寄存器）

| 指令 | 编码条件 | 语义（简写） | 实现路径 |
|---|---|---|---|
| `add` | `opcode=0x33, funct3=0, funct7=0x00` | `x[rd]=x[rs1]+x[rs2]` | `src/rv32emu_cpu_exec.c:920` |
| `sub` | `opcode=0x33, funct3=0, funct7=0x20` | `x[rd]=x[rs1]-x[rs2]` | `src/rv32emu_cpu_exec.c:924` |
| `sll` | `opcode=0x33, funct3=1` | 左移 `rs2[4:0]` | `src/rv32emu_cpu_exec.c:931` |
| `slt` | `opcode=0x33, funct3=2` | 有符号比较置位 | `src/rv32emu_cpu_exec.c:934` |
| `sltu` | `opcode=0x33, funct3=3` | 无符号比较置位 | `src/rv32emu_cpu_exec.c:937` |
| `xor` | `opcode=0x33, funct3=4` | 异或 | `src/rv32emu_cpu_exec.c:940` |
| `srl` | `opcode=0x33, funct3=5, funct7=0x00` | 逻辑右移 `rs2[4:0]` | `src/rv32emu_cpu_exec.c:943` |
| `sra` | `opcode=0x33, funct3=5, funct7=0x20` | 算术右移 `rs2[4:0]` | `src/rv32emu_cpu_exec.c:947` |
| `or` | `opcode=0x33, funct3=6` | 或 | `src/rv32emu_cpu_exec.c:954` |
| `and` | `opcode=0x33, funct3=7` | 与 | `src/rv32emu_cpu_exec.c:957` |

### 2.5 其他 RV32I 指令

| 指令 | 编码条件 | 语义（简写） | 实现路径 |
|---|---|---|---|
| `fence` / `fence.i` | `opcode=0x0f` | 当前实现为 no-op（直接成功返回） | `src/rv32emu_cpu_exec.c:979` |
| `ecall` | `raw=0x00000073` | 先尝试 `rv32emu_handle_sbi_ecall`，失败则按当前特权级抛 `ECALL_*` | `src/rv32emu_cpu_exec.c:1003` |
| `ebreak` | `raw=0x00100073` | 抛 `BREAKPOINT`，`tval=pc` | `src/rv32emu_cpu_exec.c:1016` |
| `wfi` | `raw=0x10500073` | 当前实现直接成功返回（no-op） | `src/rv32emu_cpu_exec.c:1034` |

## 3. RV32M 语义表

`opcode=0x33, funct7=0x01` 时进入 `rv32emu_exec_muldiv`。

| 指令 | funct3 | 语义（简写） | 实现路径 |
|---|---|---|---|
| `mul` | `0x0` | 低 32 位乘积 | `src/rv32emu_cpu_exec.c:303` |
| `mulh` | `0x1` | 有符号×有符号高 32 位 | `src/rv32emu_cpu_exec.c:306` |
| `mulhsu` | `0x2` | 有符号×无符号高 32 位 | `src/rv32emu_cpu_exec.c:309` |
| `mulhu` | `0x3` | 无符号×无符号高 32 位 | `src/rv32emu_cpu_exec.c:312` |
| `div` | `0x4` | 有符号除法（含除零/溢出特判） | `src/rv32emu_cpu_exec.c:315` |
| `divu` | `0x5` | 无符号除法（除零返回 `0xffffffff`） | `src/rv32emu_cpu_exec.c:324` |
| `rem` | `0x6` | 有符号余数（含除零/溢出特判） | `src/rv32emu_cpu_exec.c:327` |
| `remu` | `0x7` | 无符号余数（除零返回被除数） | `src/rv32emu_cpu_exec.c:336` |

## 4. RV32A 语义表

入口：`rv32emu_exec_amo`（`src/rv32emu_cpu_exec.c:347`），并在 `m->plat.amo_lock` 下执行原子序列（`src/rv32emu_cpu_exec.c:367`）。

| 指令 | 编码条件 | 语义（简写） | 实现路径 |
|---|---|---|---|
| `lr.w` | `opcode=0x2f, funct3=2, funct5=0x02, rs2=0` | 读字 + 建立本 hart reservation | `src/rv32emu_cpu_exec.c:371` |
| `sc.w` | `opcode=0x2f, funct3=2, funct5=0x03` | reservation 有效且地址匹配则写入并 `rd=0`，否则 `rd=1`；都清 reservation | `src/rv32emu_cpu_exec.c:394` |
| `amoswap.w` | `funct5=0x01` | `new=rs2`，`rd=old` | `src/rv32emu_cpu_exec.c:427` |
| `amoadd.w` | `funct5=0x00` | `new=old+rs2`，`rd=old` | `src/rv32emu_cpu_exec.c:430` |
| `amoxor.w` | `funct5=0x04` | `new=old^rs2`，`rd=old` | `src/rv32emu_cpu_exec.c:433` |
| `amoand.w` | `funct5=0x0c` | `new=old&rs2`，`rd=old` | `src/rv32emu_cpu_exec.c:436` |
| `amoor.w` | `funct5=0x08` | `new=old|rs2`，`rd=old` | `src/rv32emu_cpu_exec.c:439` |
| `amomin.w` | `funct5=0x10` | 有符号最小值 | `src/rv32emu_cpu_exec.c:442` |
| `amomax.w` | `funct5=0x14` | 有符号最大值 | `src/rv32emu_cpu_exec.c:445` |
| `amominu.w` | `funct5=0x18` | 无符号最小值 | `src/rv32emu_cpu_exec.c:448` |
| `amomaxu.w` | `funct5=0x1c` | 无符号最大值 | `src/rv32emu_cpu_exec.c:451` |

约束：`funct3!=2` 或地址非 4-byte 对齐时直接异常（`ILLEGAL_INST` 或 `LOAD_MISALIGNED`，见 `src/rv32emu_cpu_exec.c:360`, `src/rv32emu_cpu_exec.c:364`）。

## 5. RV32F/D（当前实现子集）语义表

### 5.1 浮点访存

| 指令 | 编码条件 | 语义（简写） | 实现路径 |
|---|---|---|---|
| `flw` | `opcode=0x07, funct3=0x2` | 读 32-bit 到 `f[rd]`，高 32 位 NaN-box | `src/rv32emu_cpu_exec.c:188` |
| `fld` | `opcode=0x07, funct3=0x3` | 读 64-bit 到 `f[rd]` | `src/rv32emu_cpu_exec.c:195` |
| `fsw` | `opcode=0x27, funct3=0x2` | 写 `f[rs2]` 低 32 位 | `src/rv32emu_cpu_exec.c:217` |
| `fsd` | `opcode=0x27, funct3=0x3` | 写 `f[rs2]` 全 64 位 | `src/rv32emu_cpu_exec.c:219` |

### 5.2 浮点运算/搬运

| 指令 | 编码条件 | 语义（简写） | 实现路径 |
|---|---|---|---|
| `fsgnj.s` | `opcode=0x53, funct7=0x10, funct3=0x0` | 单精度符号注入 | `src/rv32emu_cpu_exec.c:243` |
| `fsgnjn.s` | `opcode=0x53, funct7=0x10, funct3=0x1` | 单精度取反符号注入 | `src/rv32emu_cpu_exec.c:243` |
| `fsgnjx.s` | `opcode=0x53, funct7=0x10, funct3=0x2` | 单精度异或符号注入 | `src/rv32emu_cpu_exec.c:243` |
| `fsgnj.d` | `opcode=0x53, funct7=0x11, funct3=0x0` | 双精度符号注入 | `src/rv32emu_cpu_exec.c:261` |
| `fsgnjn.d` | `opcode=0x53, funct7=0x11, funct3=0x1` | 双精度取反符号注入 | `src/rv32emu_cpu_exec.c:261` |
| `fsgnjx.d` | `opcode=0x53, funct7=0x11, funct3=0x2` | 双精度异或符号注入 | `src/rv32emu_cpu_exec.c:261` |
| `fmv.x.w` | `opcode=0x53, funct7=0x70, funct3=0, rs2=0` | `x[rd]=f[rs1]` 低 32 位比特搬运 | `src/rv32emu_cpu_exec.c:279` |
| `fmv.w.x` | `opcode=0x53, funct7=0x78, funct3=0, rs2=0` | `f[rd]` 低 32 位来自 `x[rs1]`，高 32 位 NaN-box | `src/rv32emu_cpu_exec.c:285` |

## 6. RV32C（压缩）语义表

入口：`rv32emu_exec_compressed`（`src/rv32emu_cpu_exec.c:476`）。

### 6.1 Quadrant 0 (`insn[1:0]=00`)

| 指令 | 语义（简写） | 实现路径 |
|---|---|---|
| `c.addi4spn` | `x[rd']=x2+nzuimm` | `src/rv32emu_cpu_exec.c:490` |
| `c.fld` | `f[rd']=mem64[x[rs1']+uimm]` | `src/rv32emu_cpu_exec.c:500` |
| `c.lw` | `x[rd']=mem32[x[rs1']+uimm]` | `src/rv32emu_cpu_exec.c:505` |
| `c.flw` | `f[rd']=mem32[x[rs1']+uimm]`（NaN-box） | `src/rv32emu_cpu_exec.c:516` |
| `c.fsd` | `mem64[x[rs1']+uimm]=f[rs2']` | `src/rv32emu_cpu_exec.c:522` |
| `c.sw` | `mem32[x[rs1']+uimm]=x[rs2']` | `src/rv32emu_cpu_exec.c:527` |
| `c.fsw` | `mem32[x[rs1']+uimm]=f[rs2'](low32)` | `src/rv32emu_cpu_exec.c:534` |

### 6.2 Quadrant 1 (`insn[1:0]=01`)

| 指令 | 语义（简写） | 实现路径 |
|---|---|---|
| `c.addi` / `c.nop` | `x[rd]+=imm`（`rd=0` 时等效 nop） | `src/rv32emu_cpu_exec.c:546` |
| `c.jal` | `x1=pc+2; pc+=imm` | `src/rv32emu_cpu_exec.c:551` |
| `c.li` | `x[rd]=imm` | `src/rv32emu_cpu_exec.c:555` |
| `c.addi16sp` | `x2+=nzimm` | `src/rv32emu_cpu_exec.c:559` |
| `c.lui` | `x[rd]=imm<<12`（`rd!=0`, `imm!=0`） | `src/rv32emu_cpu_exec.c:559` |
| `c.srli` | `x[rd'] >>= shamt` | `src/rv32emu_cpu_exec.c:586` |
| `c.srai` | `x[rd'] = arithmetic_shift_right` | `src/rv32emu_cpu_exec.c:594` |
| `c.andi` | `x[rd'] &= imm` | `src/rv32emu_cpu_exec.c:602` |
| `c.sub` | `x[rd'] -= x[rs2']` | `src/rv32emu_cpu_exec.c:612` |
| `c.xor` | `x[rd'] ^= x[rs2']` | `src/rv32emu_cpu_exec.c:615` |
| `c.or` | `x[rd'] |= x[rs2']` | `src/rv32emu_cpu_exec.c:618` |
| `c.and` | `x[rd'] &= x[rs2']` | `src/rv32emu_cpu_exec.c:621` |
| `c.j` | `pc+=imm` | `src/rv32emu_cpu_exec.c:633` |
| `c.beqz` | `x[rs1']==0` 则分支 | `src/rv32emu_cpu_exec.c:636` |
| `c.bnez` | `x[rs1']!=0` 则分支 | `src/rv32emu_cpu_exec.c:642` |

### 6.3 Quadrant 2 (`insn[1:0]=10`)

| 指令 | 语义（简写） | 实现路径 |
|---|---|---|
| `c.slli` | `x[rd]<<=shamt`（`rd!=0`） | `src/rv32emu_cpu_exec.c:654` |
| `c.fldsp` | `f[rd]=mem64[x2+uimm]`（`rd!=0`） | `src/rv32emu_cpu_exec.c:663` |
| `c.lwsp` | `x[rd]=mem32[x2+uimm]`（`rd!=0`） | `src/rv32emu_cpu_exec.c:672` |
| `c.flwsp` | `f[rd]=mem32[x2+uimm]`（`rd!=0`, NaN-box） | `src/rv32emu_cpu_exec.c:686` |
| `c.jr` | `pc=x[rd]&~1`（`rd!=0, rs2=0`） | `src/rv32emu_cpu_exec.c:699` |
| `c.mv` | `x[rd]=x[rs2]`（`rd!=0, rs2!=0`） | `src/rv32emu_cpu_exec.c:709` |
| `c.ebreak` | 抛 `BREAKPOINT` | `src/rv32emu_cpu_exec.c:714` |
| `c.jalr` | `x1=pc+2; pc=x[rd]&~1`（`rd!=0, rs2=0`） | `src/rv32emu_cpu_exec.c:718` |
| `c.add` | `x[rd]+=x[rs2]`（`rd!=0, rs2!=0`） | `src/rv32emu_cpu_exec.c:729` |
| `c.fsdsp` | `mem64[x2+uimm]=f[rs2]` | `src/rv32emu_cpu_exec.c:733` |
| `c.swsp` | `mem32[x2+uimm]=x[rs2]` | `src/rv32emu_cpu_exec.c:737` |
| `c.fswsp` | `mem32[x2+uimm]=f[rs2](low32)` | `src/rv32emu_cpu_exec.c:742` |

## 7. SYSTEM/CSR 与特权敏感语义（单列）

SYSTEM 子路径：`src/rv32emu_cpu_exec.c:990`，CSR 具体读改写在 `src/rv32emu_cpu_exec_system.c:42`。

| 指令 | 条件 | 特权/副作用 | 实现路径 |
|---|---|---|---|
| `csrrw/csrrs/csrrc/csrrwi/csrrsi/csrrci` | `opcode=0x73, funct3!=0` | 仅允许 `rv32emu_csr_is_implemented` 白名单 CSR；否则非法指令 | `src/rv32emu_cpu_exec_system.c:7`, `src/rv32emu_cpu_exec_system.c:42` |
| `mret` | `raw=0x30200073` | 仅 M 态允许；恢复 `MIE/MPIE/MPP`，`pc=MEPC&~1` | `src/rv32emu_cpu_exec_system.c:100` |
| `sret` | `raw=0x10200073` | U 态禁止；恢复 `SIE/SPIE/SPP`，`pc=SEPC&~1` | `src/rv32emu_cpu_exec_system.c:127` |
| `sfence.vma` | `raw & 0xfe007fff == 0x12000073` | U 态触发非法指令；其余当前实现直接成功返回 | `src/rv32emu_cpu_exec.c:1037` |

## 8. 非法指令与异常行为

- 顶层未知 opcode：`rv32emu_exec_decoded` 默认分支抛 `RV32EMU_EXC_ILLEGAL_INST`（`src/rv32emu_cpu_exec.c:1096`）。
- 各子组未知 `funct3/funct7` 统一抛非法指令（例如 `src/rv32emu_cpu_exec.c:824`, `src/rv32emu_cpu_exec.c:906`, `src/rv32emu_cpu_exec.c:950`, `src/rv32emu_cpu_exec.c:1044`）。
- 压缩指令中保留/非法编码也抛非法指令（例如 `src/rv32emu_cpu_exec.c:541`, `src/rv32emu_cpu_exec.c:649`, `src/rv32emu_cpu_exec.c:748`）。
- `c.ebreak` / `ebreak` 抛 `BREAKPOINT`（`src/rv32emu_cpu_exec.c:714`, `src/rv32emu_cpu_exec.c:1016`）。
- AMO 地址不对齐抛 `LOAD_MISALIGNED`（`src/rv32emu_cpu_exec.c:364`）。

## 9. 退休与公共后处理

无论 16-bit 或 32-bit，执行成功后都执行统一后处理：

- `pc = next_pc`
- `x0 = 0`
- `cycle += 1`, `instret += 1`
- `rv32emu_step_timer(m)`

参考：`src/rv32emu_cpu_exec.c:1099` 与 `src/rv32emu_cpu_exec.c:1127`。
