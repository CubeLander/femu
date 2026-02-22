# 0001 - 综合技术报告（现仓评估 + 新仓迁移方案）

## 1. 背景与目标

当前仓库最初是 `ICS2019` 课程实验仓库，后续叠加了 Linux/OpenSBI/BusyBox/工具链等内容。现在的目标已经变化为：

1. 做一个可跑通 **RV32 Linux** 的最简 emulator。
2. 形成可交付、可复现、可迁移的技术文档。
3. 把“开发环境依赖”收敛到仓库自动化，不依赖宿主机手工配置。
4. 从本仓库提炼成果，迁移到一个新 repo 干净推进。

## 2. 当前仓库状态快照

- 主仓分支：`pa2-experimental`
- 关键子树：`nemu/`, `linux/`, `opensbi/`, `busybox/`, `riscv-gnu-toolchain/`
- 仓库类型：混合了课程代码、外部子仓快照、临时实验代码。

### 2.1 现状问题（工程视角）

1. 目标混杂：课程 PA 目标与 Linux 全系统模拟目标并存。
2. 代码边界不清：旧 NEMU 代码与新实验代码耦合。
3. 构建入口分散：多个目录独立构建，缺少统一 orchestration。
4. 依赖路径不透明：toolchain/linux/opensbi 的依赖关系和版本锁定不清。
5. 可迁移性弱：当前结构不利于后续写论文/做正式工程化。

## 3. 依赖关系梳理（面向“可自动化”）

目标运行链路可抽象为：

1. Toolchain（交叉编译器）
2. Linux kernel（RV32 image）
3. OpenSBI（FW_DYNAMIC for rv32）
4. Rootfs/initramfs（BusyBox）
5. Emulator（待开发的新实现）

建议依赖原则：

1. **统一容器** 提供构建依赖。
2. **脚本化入口** 统一 build/test/package。
3. **产物目录固定**（`out/`）避免散落。
4. **版本固定与记录**（文档中写清 commit/tag）。

## 4. 方案决策

### 4.1 仓库策略

- 当前仓库：作为“资产提炼仓”（文档、脚本、构建资产导出）。
- 新仓库：作为“干净实现仓”（只保留 emulator 主体与必要工件）。

### 4.2 开发策略

- 不继续在 legacy NEMU 上做大改。
- 以 clean-room 方式新建 emulator 子项目推进。
- 先保可复现构建，再做功能迭代（CPU/MMU/设备/SBI）。

## 5. 本次落地内容

本次在当前仓库新增：

1. `issues/0001-comprehensive-report.md`（本报告）
2. `docs/migration/dependency-matrix.md`
3. `docs/migration/new-repo-bootstrap.md`
4. `docker/Dockerfile.dev`
5. `scripts/dev-shell.sh`
6. `scripts/build-linux.sh`
7. `scripts/build-opensbi.sh`
8. `scripts/build-rootfs.sh`
9. `scripts/takeaway.sh`
10. `scripts/config/linux-rv32-minimal.config`

## 6. 新仓库建议结构

```text
rv32linux-emu/
  docs/
    design/
    reports/
    migration/
  docker/
  scripts/
  emulator/
    include/
    src/
    tests/
  third_party/
    (可选：外部依赖的 pinned manifest，不直接塞全量源码)
  out/ (gitignore)
```

## 7. 迁移执行清单

1. 先用 `scripts/takeaway.sh` 生成可带走的文档与自动化骨架。
2. 新 repo 初始化后，先跑容器环境验证构建链路。
3. 把 Linux/OpenSBI/Rootfs 构建脚本在新 repo 验证通过。
4. 再进入 emulator 核心迭代（按里程碑推进）。

## 8. 风险与规避

1. 风险：工具链构建过慢。
   - 规避：优先容器内使用 distro cross toolchain，工具链源码构建作为后备。
2. 风险：Linux 配置过重导致 emulator 开发难度失控。
   - 规避：先最小化 RV32 config（关 SMP/向量/FPU/模块等）。
3. 风险：旧仓改动噪声干扰新目标。
   - 规避：新仓 clean-room，旧仓仅作资产提炼。

## 9. 下一步（建议）

1. 在新 repo 创建 Milestone-0：容器 + 脚本 + 可重复产物。
2. Milestone-1：emulator 可跑裸机测试与串口输出。
3. Milestone-2：接通 Linux early boot。
4. Milestone-3：进入 initramfs shell。
5. Milestone-4：整理最终技术报告与实验数据。
