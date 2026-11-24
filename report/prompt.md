# Lab5 提示词记录

## A 线——特权级边界线：用户态系统调用与返回（2412737）

本节负责的文件为 `kern/trap/trapentry.S`、`kern/trap/trap.c`、`kern/syscall/syscall.c`。以下按四段式规范化提示词模板拆分为 5 个递增步骤，逐步指引 AI 完成上下文对齐、单文件实现与编译验收。

### 第 1 步：只读对齐上下文与边界（暂不修改代码）

```text
[PROMPT]
任务：只读分析 lab5 的 A 线任务范围、trapframe 布局、sscratch 换栈约定以及与 B/C 线的接口边界。
功能目标：在动手改代码前，先确认特权级切换与系统调用的上下文前提，避免 AI 越界修改或过度扩展无关知识。
修改范围：不修改任何源码文件。
操作要求：仅读取指定文件并针对下列问题作答；严格只答所问，不要展开完整流程。
验收方式：逐条回答清楚下列问题，并给出对应的源码位置依据。

[RELY]
依据当前工程中的真实定义：
- docs/lab5-workflow.md（A/B/C 三线分工与接口约定）
- kern/trap/trapentry.S（SAVE_ALL、__alltraps、__trapret）
- kern/trap/trap.c、kern/trap/trap.h（struct trapframe）
- kern/process/proc.c（init_main、user_main 状态确认）

[GUARANTEE]
本步不生成或修改代码，仅输出以下问题的核对结论：
1. struct trapframe 的 36 个槽如何分配，sp 与内核栈顶的关系是什么？
2. 用户态为什么会有内核栈顶，SAVE_ALL 的 csrrw s0, sscratch, x0 如何与 RESTORE_ALL 配合？
3. init_main、user_main、do_fork、do_* 分别属于哪条线，A 线是否采用 ebreak 路径？

[SPECIFICATION]
约束：
- 只根据当前仓库真实代码作答，未实现的函数如实说明尚未实现。
- 每条回答控制在核心因果关系内，不额外铺陈 B/C 线的实现细节。
```

### 第 2 步：实现 `trapentry.S` 的 `RESTORE_ALL` 双向返回与中文注释

```text
[PROMPT]
任务：在 code/kern/trap/trapentry.S 中完成 lab5 的 RESTORE_ALL 双向返回，并补充中文注释。
功能目标：使 __trapret 既能返回内核态，也能正确返回用户态，并在返回用户态前把内核栈顶写入 sscratch。
修改范围：仅修改 code/kern/trap/trapentry.S；将 A 线的 lab5 function1 YOUR CODE 替换为 lab5 function1 2412737；保留其他已有学号标记与 SAVE_ALL 原有逻辑。
操作要求：修改实际文件，用中文解释 sscratch 约定、36 槽布局与 SPP 分支方向。
验收方式：使用 riscv64-unknown-elf-gcc（含 -march=rv64imac_zicsr）汇编并 objdump 检查，确认 sstatus/sepc 偏移为 256/264、内核栈顶偏移为 288；若后续仅调整注释，需验证机器码完全一致。

[RELY]
依据当前工程中的真实定义：
- 相关宏定义：SSTATUS_SPP、REGBYTES
- 相关数据结构：struct trapframe（36 个槽，槽 2 为原 sp，槽 32~35 为 sstatus、sepc、stval、scause）
- 模块约束：SAVE_ALL 进入内核时已将 sscratch 清零，且此时 sp 始终指向内核栈上的 trapframe。

[GUARANTEE]
必须修改的宏：
.macro RESTORE_ALL

[SPECIFICATION]
函数：RESTORE_ALL

Pre-Condition（前置条件）：
- sp 指向位于内核栈顶的 trapframe。

Post-Condition（后置条件）：
- 通用寄存器、sstatus、sepc 被恢复；sscratch 按返回目标特权级保持正确约定。

Case 1（SPP=0，返回用户态）：
- bnez s0, _restore_context 不跳转，顺序执行 _save_kernel_sp，把 sp + 36*REGBYTES（内核栈顶）写入 sscratch。

Case 2（SPP=1，返回内核态）：
- bnez s0, _restore_context 跳转至 _restore_context，不修改 sscratch（保持 SAVE_ALL 清零后的 0）。
```

### 第 3 步：实现 `trap.c` 的 `exception_handler` 与中文注释

```text
[PROMPT]
任务：在 code/kern/trap/trap.c 中完成 exception_handler 对用户态系统调用与访存异常的处理，并为 trap() 相关部分补充中文注释。
功能目标：捕获用户态 ecall 并转交 syscall()；区分用户态与内核态访存异常的处理策略。
修改范围：仅修改 code/kern/trap/trap.c；将 A 线的 lab5 function1 YOUR CODE 替换为 lab5 function1 2412737；不修改 proc.c 等其他文件，也不实现 CAUSE_BREAKPOINT（ebreak）路径。
操作要求：修改实际文件；对 trap_dispatch 与 trap() 中和 A 线相关的状态检查（current->tf、in_kernel、PF_EXITING、need_resched）补充中文注释。
验收方式：确认 epc 前移位置在 syscall() 之前，编译 trap.c 无新增警告或错误。

[RELY]
依据当前工程中的真实定义：
- 相关宏定义：CAUSE_USER_ECALL、CAUSE_LOAD_ACCESS、CAUSE_STORE_ACCESS 等异常码、PF_EXITING
- 可调用的已有函数：syscall()、print_trapframe()、panic()
- 相关全局变量：current
- 模块约束：本实验由 C 线构造 trapframe 后直接跳 __trapret 启动用户进程，不走 ebreak 路径；panic() 会打印信息并关机，不会跳过继续执行。

[GUARANTEE]
必须修改的函数：
void exception_handler(struct trapframe *tf)

[SPECIFICATION]
函数：exception_handler

Case 1（CAUSE_USER_ECALL）：
- 必须先执行 tf->epc += 4，再调用 syscall()，保证 sys_fork 复制出的子进程 trapframe 不会在返回后重复执行 ecall。

Case 2（用户态访存异常）：
- 置 current->flags |= PF_EXITING 以终止当前用户进程，不触发内核 panic。

Case 3（内核态访存异常）：
- 调用 panic() 终止系统。
```

### 第 4 步：实现 `syscall.c` 的 `syscall()` 分发与 `sys_*` 处理函数

```text
[PROMPT]
任务：在 code/kern/syscall/syscall.c 中实现 syscall() 分发表与各 sys_* 系统调用入口函数。
功能目标：从 trapframe 中按 RISC-V 约定读取调用号和参数，完成用户指针合法性检查后转发至对应的 do_* 内核函数，并将返回值写回 a0。
修改范围：仅修改 code/kern/syscall/syscall.c；将 A 线的 lab5 function1 YOUR CODE 替换为 lab5 function1 2412737；不实现 do_* 系列函数，不修改 pmm.c、vmm.c、proc.c。
操作要求：修改实际文件；解释 static int (*syscalls[])(uint64_t arg[]) 的初始化方式以及各 sys_* 与 B/C 线 do_* 的分工。
验收方式：编译 syscall.c 无新增警告或错误；核对参数寄存器顺序与错误码返回路径。

[RELY]
依据当前工程中的真实定义：
- 相关宏定义：SYS_exit、SYS_fork、SYS_wait、SYS_exec、SYS_yield、SYS_kill、SYS_getpid、SYS_putc、SYS_pgdir、E_INVAL
- 可调用的已有函数：do_exit、do_fork、do_wait、do_execve、do_yield、do_kill、user_mem_check、cputchar、print_trapframe
- 相关全局变量：current
- 模块约束：a0 为调用号，a1~a5 为参数，返回值写回 tf->gpr.a0；pmm.c 尚未实现 print_pgdir() 时 sys_pgdir 暂返回 0。

[GUARANTEE]
必须实现或修改的函数：
void syscall(void)
static int sys_exit(uint64_t arg[])
static int sys_fork(uint64_t arg[])
static int sys_wait(uint64_t arg[])
static int sys_exec(uint64_t arg[])
static int sys_yield(uint64_t arg[])
static int sys_kill(uint64_t arg[])
static int sys_getpid(uint64_t arg[])
static int sys_putc(uint64_t arg[])
static int sys_pgdir(uint64_t arg[])

辅助函数要求：允许在 syscall.c 内新增 static 辅助函数用于检查用户态指针。

[SPECIFICATION]
函数：syscall

Pre-Condition（前置条件）：
- current 与 current->tf 非空。

Post-Condition（后置条件）：
- 合法调用号：通过函数指针数组 syscalls[] 分发调用，并将返回值写入 tf->gpr.a0。
- 非法调用号（负数、越界或表项为 NULL）：打印现场并返回 -E_INVAL，不触发 panic。

函数：sys_wait、sys_exec

Requirements（指针与状态约束）：
- sys_wait 的 store 指针必须经 user_mem_check 检查可写，因为内核需写入子进程退出码；不合法返回 -E_INVAL。
- sys_exec 的 name 与 binary 指针必须经 user_mem_check 检查可读；不合法返回 -E_INVAL。
- sys_yield 仅通过 do_yield 置位 need_resched，由 trap() 返回前调度；sys_kill 通过 do_kill 置位 PF_EXITING，目标进程在下次经 trap() 返回用户态（含时钟中断）时退出。
```

### 第 5 步：范围复核、编译验收与调试前置确认

```text
[PROMPT]
任务：对第 2~4 步的改动执行编译与边界复核，并确认当前阶段是否具备 QEMU/GDB 运行条件。
功能目标：确保 A 线三个文件独立编译通过且未越界改动 B/C 线代码，避免在依赖未就绪时强行运行或虚构调试结果。
修改范围：不新增功能代码；仅运行检查命令并如实报告结果。
操作要求：检查 git 状态、学号标记替换情况，并执行编译。
验收方式：提供实际命令输出，明确区分“已编译通过”与“因 B/C 线未实现而预期失败的链接阶段”。

[RELY]
依据当前工程中的真实定义：
- 编译命令：make clean && make
- 分工约束：B 线与 C 线的 do_exit、do_wait、do_execve、do_yield、do_kill 等尚未实现。

[GUARANTEE]
输出以下四项核对结果：
1. git diff / git status 确认仅改动 A 线负责的三个文件（如存在非本会话改动需如实指出）。
2. grep 确认 A 线三处 lab5 function1 2412737 标记已替换，B/C 线的 YOUR CODE 未被误改。
3. make clean && make 的真实编译与链接结果（三个目标文件编译通过，链接因 do_* 未定义失败属预期）。
4. 明确当前尚不能进行 QEMU / GDB 端到端测试（需等 B、C 完成并达到 M1 里程碑），仅可准备后续 GDB 观察命令与对照清单。
```
