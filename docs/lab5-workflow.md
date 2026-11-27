# lab5 三人协作 Workflow

> 日期：2026-11-23。依据：`docs/lab5.md` 与 `origin/lab5` 骨架（只读核对，未切换分支）。
> 用法：每人只更新自己名下的清单，同步时看勾选即可。`[x]` 表示已编译通过并自测。

## 分工总览

| 角色 | 主线 | 一句话职责 | 负责人 |
|---|---|---|---|
| A | 特权级边界线 | 用户态进出内核：ecall、trap 返回、系统调用分发、gdb 分支任务 | |
| B | 地址空间线 | 用户进程的内存：ELF 装载、地址空间复制与回收、COW | |
| C | 进程生命线 | 进程状态机：创建、fork、wait、exit、kill、yield、调度 | |

## 开工前先对齐的接口

三人先约定下面几项，再各自写代码。这是三条线之间唯一的窄接口。

- [ ] trapframe 约定：`load_icode`（B）设置 `epc = ELF 入口`、`sp = USTACKTOP`、`status` 中 `SPP=0`、`SPIE=1`；`RESTORE_ALL`（A）按此返回用户态
- [ ] `do_fork`（C）复制 trapframe 后令子进程 `a0 = 0`、`sp` 指向新栈，由 C 写、A 复核
- [ ] `do_wait` 与 `do_exit`（均归 C）的状态位约定：`PROC_ZOMBIE`、`WT_CHILD`、`PF_EXITING`
- [ ] 系统调用参数约定：`a0` 为调用号，`a1`~`a5` 为参数，返回值写回 `tf->gpr.a0`（A）
- [ ] `proc.c` 由 B、C 同时修改。B 负责 `copy_mm`、`load_icode`、`do_execve`。C 合并 `do_fork` 时保留 `copy_mm` 成功后、进程登记失败时的地址空间回收

## A：特权级边界线

涉及文件：`kern/trap/trapentry.S`、`kern/trap/trap.c`、`kern/syscall/syscall.c`

### 承接的前序实验代码

- [x] `trapentry.S` 中 `SAVE_ALL` 之后的 `__alltraps` 补全（lab3 遗留，骨架约 L75）
- [x] `trap.c` 中 lab3 遗留部分（骨架约 L98）

### lab5 功能

- [x] `RESTORE_ALL`：扩充为用户态与内核态双向返回（恢复 `sstatus`、`sepc`，按 `SPP` 决定 `sscratch`）
- [x] `exception_handler`：处理 `CAUSE_USER_ECALL`，`epc += 4`，转交 `syscall`
- [x] 若骨架使用 `ebreak` 进入 `kernel_execve` 路径，处理 `CAUSE_BREAKPOINT` 分支，与 C 对接
- [x] `syscall()`：取调用号与参数，边界检查，调用分发表，返回值写回 `a0`
- [x] `sys_exit`
- [x] `sys_fork`
- [x] `sys_wait`
- [x] `sys_exec`
- [x] `sys_yield`
- [x] `sys_kill`
- [x] `sys_getpid`
- [x] `sys_putc`
- [x] `sys_pgdir`

### 分支任务 gdb

- [ ] `add-symbol-file obj/__user_exit.out`，在用户态 `syscall` 的 `ecall` 前停住
- [ ] 观察 QEMU 处理 `ecall` 的流程并阅读相关源码
- [ ] 观察 QEMU 处理 `sret` 的流程并阅读相关源码
- [ ] 了解 TCG Translation，并与 lab2 的双重 gdb 实验对照
- [ ] 记录有趣细节、AI 交互过程，整理进报告

## B：地址空间线

涉及文件 `kern/mm/default_pmm.c`、`kern/mm/pmm.c`、`kern/mm/vmm.c` 和 `kern/process/proc.c`。

### 承接的前序实验代码

- [x] `default_pmm.c` 中 lab2 遗留（骨架约 L6）
- [x] `pmm.c` 中 lab2 遗留（骨架约 L90）
- [x] `pmm.c` 中 lab4 遗留的页表映射代码

### lab5 功能

- [x] `copy_range`（`pmm.c`）逐页复制内容并保留映射权限。临时 QEMU 测试验证了父子页独立、空洞保留和 10 个分配失败点的回收。
- [x] `dup_mmap`（`vmm.c`）复制 VMA 及其映射，保持区间顺序、权限和栈保护页。测试覆盖了 VMA、物理页和页表的分配失败，以及回滚后重试。
- [x] `copy_mm`（`proc.c`）覆盖内核线程、`CLONE_VM` 共享和普通 `fork` 独立复制。测试覆盖了 8 个分配失败点，以及 `do_fork` 在复制后达到进程数上限时对独立 `mm` 或共享引用的回收。
- [x] `load_icode`（`proc.c`）：校验 ELF、映射程序段并清零 BSS、建立用户栈、设置首次进入用户态的 trapframe
- [x] `do_execve`（`proc.c`）：检查输入，换页表前保存用户态传入的 ELF，释放对旧 `mm` 的引用，再调用 `load_icode`

装载部分已编译并在临时 QEMU 副本中验证。使用临时启动入口和退出处理，`hello` 与 `testbss` 都进入了用户态，覆盖了 BSS 清零、退出后的页表回收以及分配失败回滚。`exit_range()` 已修正中间页表目录的判空逻辑。

### B 线剩余任务

- [ ] 提交本地 `copy_range`、`dup_mmap` 和 `copy_mm` 的改动，并推送到远程 `lab5`，供 C 线接入。
- [ ] 等 C 线接好进程启动和 `fork`、`exit`、`wait` 后，参与 M1–M3 联调。用 `forktest` 和 `forktree` 验证完整复制与回收流程，用 `badarg` 核对 `user_mem_check` 对非法 `waitpid` 指针的拦截，并确认 `exit_mmap` 在进程退出及地址空间最后一个引用释放时正确回收。最后跑 `make grade`。
- [ ] 补写 B 线的 `report/report.md` 内容，说明 ELF 装载、地址空间复制和失败回滚。按提交要求汇总 B 线提示词到 `report/prompt.md`，联调后补齐对应截图。

### 问答与挑战

- [ ] 回答 Challenge 第 2 题：用户程序何时被预先加载，与常见操作系统的区别和原因
- [ ] 可选：COW 的 page fault 路径与状态转换图
- [ ] 可选：dirtycow 的模拟与解释

## C：进程生命线

涉及文件：`kern/process/proc.c`、`kern/process/switch.S`、`kern/schedule/sched.c`

### 承接的前序实验代码

- [x] `proc.c` 中 lab4 遗留（骨架约 L85）：`alloc_proc` 基础、`proc_init`、`proc_run`、`copy_thread`、`do_fork` 的基本框架、`cpu_idle` 等
- [x] `switch.S` 中 lab4 遗留
- [x] `sched.c` 中 lab4 遗留：`schedule`

### lab5 功能

- [ ] `alloc_proc`：新增字段初始化（`wait_state`、`cptr`、`yptr`、`optr` 等）
- [ ] `kernel_thread`：lab5 更新
- [ ] `init_main`：创建 `user_main` 并 `do_wait` 收尾
- [ ] `user_main`：选择用户二进制并调用 `kernel_execve`
- [ ] `kernel_execve`：与 A 对接，从内核线程转入 trap 返回路径
- [ ] `do_fork`：lab5 版，设置父子关系，`set_links`，`copy_mm` 调用点，`wakeup_proc`
- [ ] `do_wait`：僵尸回收、睡眠与唤醒、释放内核栈和 PCB
- [ ] `do_exit`：回收 `mm`、置僵尸态、唤醒父进程、孤儿挂到 `initproc`、调度
- [ ] `do_yield`
- [ ] `do_kill`

## 联调里程碑

按顺序推进，每个里程碑三人都要在场验证。

- [ ] M0：三人把前序实验遗留代码补完并合并，`make qemu` 能启动到 idle，时钟中断正常（等价于 lab4 回归）
- [ ] M1：第一个用户程序 `hello` 能从用户态输出，`ecall` 能返回（A、B、C 的 `init_main`、`user_main`、`kernel_execve`、`load_icode`、`RESTORE_ALL`、`sys_putc` 联调）
- [ ] M2：`exit`、`yield`、`forktest`、`waitkill` 全部通过（联调 C 的进程生命周期和 B 的 `copy_mm` → `dup_mmap` → `copy_range`）
- [ ] M3：`make grade` 全部通过（`forktree`、`spin`、`testbss`、`badarg`、`badsegment`、`divzero`、`faultread`、`faultreadkernel`、`softint` 也要覆盖）
- [ ] M4：gdb 分支任务完成，可选 COW 完成

## 提交与报告

- [ ] 骨架分支：`origin/lab5` 已获取；A 或 C 切到 `lab5` 并确认当前 `lab4` 分支上的未提交改动（`code/kern/mm/vmm.h`）已妥善保存，再切换
- [ ] 提交时保留 `lab5` 与 `YOUR CODE` 注释，把 `YOUR CODE` 替换为学号
- [ ] `report/report.md`：整体逻辑线、各功能核心函数、spec 与 prompt、AI 生成的 bug 及解决、知识点对应、未对应的知识点；三人分别写自己那条线
- [ ] `report/prompt.md`：汇总每个人的全部提示词，包括失败尝试
- [ ] `report/images/`：`make qemu`、`make grade` 与 gdb 的截图，报告用相对路径引用
- [ ] 提交前检查：`docs/`、`class/`、`tmp/` 与个人文件均未被加入仓库
