# Lab4 提示词记录

## 模块二——初始化进程控制块 alloc_proc()

### 规范化提示词

```text
[PROMPT]
任务：在当前 Lab4 工程的 code/kern/process/proc.c 中，
完成模块二的第一步，只实现进程控制块分配函数 alloc_proc()。

功能目标：申请一个独立的进程控制块，并将其初始化为安全、
尚未登记且不可调度的状态，为后续分配内核栈、设置初始现场
和创建内核线程提供基础。

修改范围：
- 只在 proc.c 的模块二预留位置实现 alloc_proc()。
- 保留 LAB4 function2 注释，将 YOUR CODE 替换为 2412832。
- 保持 proc_struct、现有函数接口以及已有辅助函数不变。
- 不实现其余五个模块二函数，不修改模块一和模块三。

操作要求：先阅读 proc.h、kmalloc.h、pmm.h、string.h 和 list.h，
确认字段含义与接口定义，再修改实际代码，添加必要的中文注释。

验收方式：使用当前 RISC-V 交叉编译器和工程编译参数编译 proc.c，
检查返回路径及各字段初始状态。由于其余函数尚未实现，
本轮先完成单文件编译检查，完整运行测试留待后续模块接入。

[RELY]
依据当前工程中的真实定义：

1. 数据结构与状态：
   code/kern/process/proc.h 定义 struct proc_struct，包含：
   state、pid、runs、kstack、need_resched、parent、mm、context、
   tf、pgdir、flags、name、list_link 和 hash_link。
   PROC_UNINIT 表示尚未完成初始化的进程状态。
   context 保存 ra、sp 和 s0 至 s11。

2. 已有接口：
   void *kmalloc(size_t n);
   void *memset(void *s, char c, size_t n);
   static inline void list_init(list_entry_t *elm);
   kmalloc() 用于申请 PCB，memset() 可用于初始化结构体，
   list_init() 将链表节点初始化为自环。

3. 全局变量：
   extern uintptr_t boot_pgdir_pa;
   其定义位于 kern/mm/pmm.c，表示内核启动页表的物理地址。

4. 当前阶段的约束：
   内核线程共用内核地址空间，没有独立的用户 mm。
   alloc_proc() 只负责 PCB 的分配和基础初始化，
   内核栈、正式 PID、父线程和初始执行现场由后续流程设置。

[GUARANTEE]
必须实现的函数：
static struct proc_struct *alloc_proc(void);

辅助函数要求：
复用已有内存分配、内存初始化和链表接口，
不新增辅助函数或对外接口。

[SPECIFICATION]
函数：alloc_proc()

Pre-Condition（前置条件）：
- 物理内存管理和 kmalloc 初始化已经完成，可以申请 PCB 内存。
- boot_pgdir_pa 已指向可用的内核启动页表。

Post-Condition（后置条件）：
- 成功时返回一个独立、有效的 struct proc_struct 指针。
- state 为 PROC_UNINIT，pid 为 -1。
- runs、kstack、need_resched 和 flags 为 0。
- parent、mm 和 tf 为 NULL。
- context 的全部寄存器字段为 0，name 数组全部为 0。
- pgdir 等于 boot_pgdir_pa，保存物理地址而非虚拟地址。
- list_link 和 hash_link 均为独立的自环节点。
- PCB 尚未加入全局进程链表或 PID 哈希表。
- 分配失败时返回 NULL，不访问空指针。

Case 1（正常情况）：
- kmalloc() 成功，返回字段均满足后置条件的 PCB。
- 不为其分配正式 PID、内核栈或独立页表，不使其变为 runnable。

Case 2（分配失败）：
- kmalloc() 返回 NULL 时，函数直接返回 NULL。
- 不初始化无效内存，不改变现有线程及进程管理全局状态。

Case 3（多次调用）：
- 各次成功返回的 PCB 相互独立。
- 初始化新 PCB 不改变之前已经申请的 PCB。

Requirements（其他约束）：
- 覆盖所有字段，避免未初始化内存进入后续创建流程。
- 链表节点需要通过 list_init() 初始化，不能只清零指针。
- 不修改 current、idleproc、initproc、nr_process 和全局链表。
- 保留函数的 static 作用域，不提前实现线程登记、唤醒或调度。
```

### 执行结果

- 修改文件：`code/kern/process/proc.c`。
- 实现内容：分配 PCB，失败时返回 `NULL`；成功后清零结构体，设置初始状态、PID 和内核页表物理地址，并初始化两个链表节点。
- 任务注释已改为 `//LAB4 function2 2412832`。
- 使用当前 RISC-V 工具链和工程编译参数，完成了 `proc.c` 的单文件编译；对象文件保存在 WSL 临时目录 `/tmp/lab4-alloc-proc.y5ODHq/proc.o`。
- `git diff --check` 通过。
- 后续步骤：实现 `copy_mm()` 和 `copy_thread()`，再接入线程创建流程，检查创建出的 PCB 与初始执行现场。

## 模块二——处理内核线程地址空间 copy_mm()

### 规范化提示词

```text
[PROMPT]
任务：在当前 Lab4 工程的 code/kern/process/proc.c 中，
接着已实现的 alloc_proc()，只实现 copy_mm()。

功能目标：提供线程创建流程需要的地址空间处理接口。
当前实验只创建内核线程，应检查其没有独立用户地址空间，
复用已有的内核页表，不进行用户地址空间的复制或共享计数管理。

修改范围：
- 在模块二区域补充 copy_mm()，保留学号注释 2412832。
- 保留 alloc_proc() 的实现和注释，不修改其他模块。
- 不提前实现 copy_thread()、do_fork()、kernel_thread() 或 proc_init()。

操作要求：阅读 proc.h、vmm.h、pmm.h 和 alloc_proc()，
确认 mm 和 pgdir 的区别，补充适量中文注释。

验收方式：使用工程对应的 RISC-V 编译参数编译 proc.c，
检查内核线程情况下的返回值、约束检查和资源状态。
后续创建流程接入后，再进行运行验证。

[RELY]
依据当前工程中的真实定义：

1. struct proc_struct 的相关字段：
   struct mm_struct *mm：描述独立用户地址空间的管理结构指针。
   uintptr_t pgdir：线程使用的页表物理地址。

2. current 指向当前线程，新线程 PCB 由 alloc_proc() 创建。
   alloc_proc() 已将新 PCB 的 mm 置为 NULL，
   并将 pgdir 设置为 boot_pgdir_pa。

3. CLONE_VM 定义在 pmm.h 中，用于表示共享地址空间。
   当前实验没有用户地址空间复制逻辑，clone_flags 先作为预留参数。

4. 可使用 assert() 检查当前阶段的内核线程约束。
   mm == NULL 不表示分页关闭，内核线程仍使用 pgdir 指向的内核页表。

[GUARANTEE]
必须实现的函数：
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);

辅助函数要求：
无需新增辅助函数，不调用用户地址空间分配、复制和释放接口。

[SPECIFICATION]
函数：copy_mm()

Pre-Condition（前置条件）：
- current 已设置为一个有效的内核线程，current->mm == NULL。
- proc 为 alloc_proc() 创建且尚未登记的新 PCB，proc->mm == NULL。
- proc->pgdir 已指向可用的内核页表。

Post-Condition（后置条件）：
- 在满足当前实验约束时返回 0。
- proc->mm 保持 NULL，proc->pgdir 保持原值。
- 不申请或释放内存，不修改当前线程和进程管理全局结构。

Case 1（正常情况）：
- 当前线程和新 PCB 均没有用户 mm，检查通过并返回 0。
- 当前阶段不根据 clone_flags 改变行为，显式标记其为预留参数。

Case 2（违反当前阶段约束）：
- current 或 proc 无效，或其中存在用户 mm 时，使用断言终止，
  避免将尚未实现的用户地址空间处理误判为成功。

Requirements（其他约束）：
- 保持 static 作用域及已有函数签名。
- 不通过清空已有用户 mm 指针的方式假装完成复制，避免丢失资源。
- 注释说明 mm 与 pgdir 的区别，以及用户地址空间处理留待后续扩展。
```

### 执行结果

- 修改文件：`code/kern/process/proc.c`，在模块二的学号注释区域新增 `copy_mm()`。
- 实现内容：保留 `clone_flags` 参数，通过断言检查当前线程与新 PCB 均没有用户地址空间，满足约束时返回 `0`；保留原有 `mm` 和 `pgdir`。
- 编译结果：使用当前 RISC-V 工具链及工程参数编译 `proc.c` 成功，对象文件位于 WSL 临时目录 `/tmp/lab4-copy-mm.fYveVf/proc.o`。
- 验证阶段：本轮完成单文件编译与代码检查，运行验证将在调用流程接入后进行。
- 后续步骤：实现 `copy_thread()`，把新线程的初始现场放到独立内核栈中。

## 模块二——准备线程初始现场 copy_thread()

### 规范化提示词

```text
[PROMPT]
任务：在当前 Lab4 的 code/kern/process/proc.c 中，
接着 alloc_proc() 和 copy_mm()，只实现 copy_thread()。

功能目标：将传入的初始 trapframe 复制到新线程自己的内核栈，
设置子线程返回值和运行栈，并配置首次上下文切换的入口与栈地址，
让后续调度能衔接 forkret -> forkrets -> __trapret 的恢复链路。

修改范围：
- 在 LAB4 function2 2412832 区域新增 copy_thread()。
- 保留已完成函数和现有汇编接口，不实现剩余三个模块二函数。
- 不修改模块一和模块三，不改动原有 trap 保存与恢复代码。

操作要求：阅读 proc.h、trap.h、memlayout.h、proc.c 中的 forkret()，
以及 trapentry.S 和 process/entry.S，确认结构布局与首次运行链路。
注释说明栈布局、现场复制、a0 的含义，以及两个 sp 的不同用途。

验收方式：使用当前 RISC-V 工具链编译；在临时工程副本中运行
QEMU 检查现场布局、复制独立性、寄存器值、两种栈参数及边界保护。
测试入口放在临时副本，完整调度验证留待其他函数接入。

[RELY]
依据当前工程中的真实定义：

1. struct proc_struct 的相关字段：
   kstack：独立内核栈的起始虚拟地址。
   tf：指向新线程内核栈中的 struct trapframe。
   context：供 switch_to() 保存和恢复的上下文，包括 ra、sp、s0 至 s11。

2. KSTACKSIZE = KSTACKPAGE * PGSIZE，KSTACKPAGE 为 2。
   内核栈由 setup_kstack() 分配，栈向低地址增长。

3. struct trapframe 包含 gpr、status、epc、badvaddr 和 cause。
   RV64 下共有 36 个寄存器宽度的槽位，大小为 288 字节。
   gpr.a0 是函数返回值寄存器，gpr.sp 是恢复现场后的运行栈地址。

4. 已有恢复链路：
   forkret() 调用 forkrets(current->tf)；
   forkrets 将 sp 设置为传入的 tf，再跳转 __trapret；
   __trapret 恢复寄存器和 CSR，执行 sret 跳到 tf->epc。

5. alloc_proc() 已将 context 清零。
   初始 epc、status、函数地址及参数由调用者组织在传入的 tf 中。
   可使用 assert() 检查调用前的必要资源状态。

[GUARANTEE]
必须实现的函数：
static void copy_thread(struct proc_struct *proc, uintptr_t esp,
                        struct trapframe *tf);

辅助函数要求：
不新增辅助函数，不分配内存，不修改已有恢复链路的接口。

[SPECIFICATION]
函数：copy_thread()

Pre-Condition（前置条件）：
- proc 指向已初始化、尚未运行的新 PCB，context 已清零。
- proc->kstack 指向大小为 KSTACKSIZE 的有效独立内核栈。
- tf 指向可读且不与目标现场区域重叠的初始 trapframe。
- esp 为 0，或为调用者提供的合法运行栈地址。

Post-Condition（后置条件）：
- proc->tf 指向内核栈顶下方的一个完整 trapframe。
- 现场内容来自传入 tf，其中 gpr.a0 为 0。
- esp 为 0 时，现场中的 sp 为新线程内核栈顶；否则等于 esp。
- context.ra 为 forkret 的地址，context.sp 为 proc->tf 的地址。
- 其他 context 字段保持原值，不修改原始 tf 或 PCB 的登记状态。

Case 1（内核线程使用默认栈）：
- esp == 0，将运行栈设置为 proc->kstack + KSTACKSIZE。
- 复制后的现场独立于调用者传入的 tf。

Case 2（调用者指定栈）：
- esp != 0，将该值写入新现场的 gpr.sp。
- trapframe 本身仍存放在新线程自己的内核栈内。

Requirements（其他约束）：
- 调用资源无效时通过断言终止，不解引用无效指针。
- 按 sizeof(struct trapframe) 计算现场位置，不硬编码字节偏移。
- 不越过内核栈边界，保持当前 RV64 栈布局所需的 16 字节对齐。
- 保留传入的 epc、status、s0 和 s1 等初始信息。
- 不修改线程状态、PID、全局链表和 current，不执行实际调度。
```

### 执行结果

- 修改文件：`code/kern/process/proc.c`，在模块二新增 `copy_thread()`，保留学号 `2412832`。
- 实现内容：在独立内核栈顶部下方复制现场，设置子线程返回值和运行栈，配置 `context.ra` 与 `context.sp`，并添加必要的资源断言和中文注释。
- 验证方式：复制当前工程到 WSL 临时目录 `/tmp/lab4-copy-thread.nw0ex9rl/code`，仅在临时副本增加检查函数和启动调用，使用原工程编译参数构建并运行 QEMU。
- 检查内容：现场位置与 16 字节对齐；原始现场不变、复制内容独立；除 `a0` 和 `sp` 外的现场内容保持；默认和指定 `esp` 两种情况；`context` 入口与栈地址；现场之外的栈空间及前后保护区域不被覆盖。
- 实际结果：临时副本编译成功，QEMU 输出 `COPY_THREAD CHECK PASSED`。日志保存在临时目录中的 `build.log` 和 `qemu.log`。本轮验证了初始现场准备，实际调度与 `sret` 执行将在创建和调度流程接入后检查。
- 后续步骤：实现 `do_fork()`，组合 PCB、内核栈与初始现场，并处理失败清理、PID 分配、线程登记和唤醒。

## 模块二——创建并登记内核线程 do_fork()

### 规范化提示词

```text
[PROMPT]
任务：在当前 Lab4 的 code/kern/process/proc.c 中，
接着已经实现的 alloc_proc()、copy_mm() 和 copy_thread()，
只实现 do_fork()，串联内核线程的创建流程。

功能目标：为新线程申请 PCB 和独立内核栈，处理地址空间与初始现场，
分配正式 PID，登记到进程链表和 PID 哈希表，最后设置为可运行。
任何可恢复失败都应返回负错误码，并释放本次已经申请的资源。

修改范围：
- 在 LAB4 function2 2412832 区域补充 do_fork()。
- 复用前三个函数及框架已有辅助函数，保留当前实现与注释。
- 不实现 kernel_thread() 和 proc_init()，不修改模块一和模块三。

操作要求：先阅读 get_pid()、hash_proc()、setup_kstack()、
put_kstack()、wakeup_proc() 与中断保护接口，确认资源所有权和登记顺序。
添加适量中文注释，说明正常流程、关键临界区和失败清理路径。

验收方式：使用当前 RISC-V 工具链构建临时工程副本并运行 QEMU，
检查成功创建、PID 唯一性、链表与哈希登记、父子关系、初始现场、
中断状态恢复、数量上限和内存分配失败后的清理。
测试辅助代码只放入临时副本，不改动正式工程的启动和调度流程。

[RELY]
依据当前工程中的真实定义：

1. 创建所需接口：
   static struct proc_struct *alloc_proc(void);
   static int setup_kstack(struct proc_struct *proc);
   static void put_kstack(struct proc_struct *proc);
   static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
   static void copy_thread(struct proc_struct *proc, uintptr_t esp,
                           struct trapframe *tf);
   void kfree(void *objp);

2. 登记和唤醒接口：
   get_pid() 分配未被已有进程占用的 PID。
   hash_proc() 将 PCB 加入 PID 哈希表。
   list_add() 可将 PCB 的 list_link 加入 proc_list。
   wakeup_proc() 将尚不可运行的线程设为 PROC_RUNNABLE。

3. current 表示父线程，nr_process 为当前进程数量，
   MAX_PROCESS 为数量上限；空闲线程计入数量，但不加入 proc_list。

4. 错误码：E_NO_MEM 表示内存不足，E_NO_FREE_PROC 表示数量达到上限。
   setup_kstack() 和 copy_mm() 成功返回 0，失败返回负错误码。

5. local_intr_save() 保存原中断状态并关中断，
   local_intr_restore() 恢复原状态，适用于本实验单核的登记临界区。
   当前 copy_mm() 仅支持没有用户 mm 的内核线程，不分配地址空间资源。

[GUARANTEE]
必须实现的函数：
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);

辅助函数要求：
复用已有接口，不新增正式工程的辅助函数或测试钩子。

[SPECIFICATION]
函数：do_fork()

Pre-Condition（前置条件）：
- current 指向有效的父内核线程，tf 指向有效的初始寄存器现场。
- 内存分配器、proc_list 和 hash_list 已初始化。
- current->mm == NULL，stack 的含义遵循 copy_thread() 的约定。

Post-Condition（后置条件）：
- 成功返回正 PID，find_proc() 可找到对应 PCB。
- 新线程的 parent 为 current，拥有独立内核栈及准备好的初始现场。
- PCB 已加入链表和哈希表，nr_process 增加 1，状态为 PROC_RUNNABLE。
- current 和调用者的 tf 保持不变，不直接执行线程切换。
- 失败返回负错误码，不留下本次创建的 PCB、内核栈或登记节点。
- 进入登记临界区前的中断状态在退出时得到恢复。

Case 1（正常创建）：
- 资源全部准备成功后，再保护 PID 分配、全局登记、计数更新和唤醒。
- 多次创建的线程 PID 不重复，独立内核栈互不重叠。

Case 2（数量达到上限）：
- 开始时已满，直接返回 -E_NO_FREE_PROC，不申请 PCB。
- 准备资源期间数量达到上限，登记前重新检查，释放本次资源后返回。

Case 3（资源准备失败）：
- PCB 分配失败，返回 -E_NO_MEM。
- 内核栈分配失败，释放 PCB，返回分配错误码。
- copy_mm() 返回错误时，释放内核栈和 PCB，保留其错误码。
  当前用户 mm 不受支持的情况仍由 copy_mm() 的断言处理。

Requirements（其他约束）：
- 先准备资源，再登记，最后唤醒，避免调度器看到半初始化线程。
- 按申请的逆序清理资源，不重复释放，不回收父线程的资源。
- 对全局结构的保护针对当前单核环境，不将关本地中断视为多核锁。
- 使用 kfree() 释放 PCB，使用 put_kstack() 释放独立内核栈。
```

### 执行结果

- 修改文件：`code/kern/process/proc.c`，在模块二新增 `do_fork()`，保留学号 `2412832`。
- 实现内容：依次准备 PCB、父线程指针、内核栈、地址空间和初始现场，再在中断保护下检查上限、分配 PID、登记、更新计数并唤醒；失败时逆序释放已申请资源。
- 验证方式：在 `/tmp/lab4-do-fork.ux1uj05b/code` 临时副本中运行检查，临时设置父线程和管理结构，并对 PCB、内核栈分配注入失败。正式工程未加入测试钩子，也未补写剩余函数。
- 正常行为：连续创建三个线程，检查 PID 不重复、栈独立、父子关系、可运行状态、页表及初始现场；确认链表数量、哈希查找和进程计数一致，原始 `tf` 不变。
- 边界行为：检查默认与指定栈地址、原中断开启与关闭两种状态的恢复；检查 PCB 分配失败、内核栈分配失败、初始数量上限和准备期间触及上限。手动回收测试线程后，空闲页数恢复基线，测试记录的 PCB 数归零，链表与哈希表为空。
- 实际结果：临时副本编译成功，QEMU 输出 `DO_FORK CHECK PASSED`；`build.log` 和 `qemu.log` 保存在对应临时目录。当前 `copy_mm()` 没有可恢复错误返回，其未来错误返回的清理分支经过代码检查，本轮未注入该情况。实际调度执行留待后续函数接入。
- 后续步骤：实现 `kernel_thread()`，将函数入口和参数组织成初始现场并调用 `do_fork()`。

## 模块二——包装内核线程入口 kernel_thread()

### 规范化提示词

```text
[PROMPT]
任务：在当前 Lab4 的 code/kern/process/proc.c 中，
接着已经实现的 do_fork()，只实现 kernel_thread()。

功能目标：将普通 C 函数 fn 和参数 arg 包装成内核线程的初始现场，
准备正确的首次入口与特权状态，再调用 do_fork() 完成创建。

修改范围：
- 在 LAB4 function2 2412832 区域补充 kernel_thread()。
- 保留前四个函数与现有汇编代码，不实现 proc_init() 或模块三。

操作要求：对照 process/entry.S 和 trapentry.S 的实际恢复流程，
解释函数地址、参数、epc 和 sstatus 的设置，添加适量中文注释。

验收方式：在临时副本构建并运行 QEMU，检查创建出的初始现场，
并通过已有恢复链路实际进入测试函数，确认参数、栈和中断状态。
测试入口与辅助代码只放入临时副本。

[RELY]
依据当前工程中的真实定义：

1. process/entry.S 中 kernel_thread_entry 执行：
   move a0, s1
   jalr s0
   jal do_exit
   因此初始 s0 应保存函数地址，s1 应保存参数。

2. struct trapframe 包含 gpr、status、epc、badvaddr 和 cause。
   __trapret 恢复其寄存器及 CSR，执行 sret 跳转到 epc。

3. sstatus 位定义：
   SSTATUS_SPP：控制 sret 返回的特权级。
   SSTATUS_SPIE：sret 时恢复给 SIE 的值。
   SSTATUS_SIE：S 态全局中断使能位。
   read_csr(sstatus) 可读取当前状态。

4. CLONE_VM 表示共享地址空间。
   do_fork(clone_flags, stack, tf) 成功返回正 PID，失败返回负错误码。
   stack 为 0 时，copy_thread() 使用新线程自己的内核栈顶，
   并将临时 tf 复制到独立内核栈。

5. 可使用 memset() 初始化临时现场，E_INVAL 表示无效参数。

[GUARANTEE]
必须实现的函数：
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);

辅助函数要求：
复用 do_fork() 和现有恢复链路，不新增正式工程的辅助接口。

[SPECIFICATION]
函数：kernel_thread()

Pre-Condition（前置条件）：
- 调用环境满足 do_fork() 的内核线程创建条件。
- 非空 fn 为有效的 int (*)(void *) 函数，arg 可以为 NULL。

Post-Condition（后置条件）：
- 合法请求的初始 s0 为 fn、s1 为 arg，epc 为 kernel_thread_entry。
- 初始 status 在当前 sstatus 基础上设置 SPP 和 SPIE，清除 SIE，
  保留其余位；函数本身不写当前线程的 sstatus。
- 其余初始现场字段先清零，再由 copy_thread() 设置子线程返回值和栈。
- 调用 do_fork(clone_flags | CLONE_VM, 0, &tf)，原样返回创建结果。

Case 1（正常创建）：
- 函数及参数正确保存，临时现场经 do_fork() 复制后独立存在。
- 空参数合法，调用者传入的其他 clone 标志得到保留。
- 新线程恢复后在 S 态调用 fn(arg)，使用自己的内核栈，中断处于开启状态。

Case 2（无效函数）：
- fn == NULL 时返回 -E_INVAL，不申请资源、不登记线程。

Case 3（创建失败）：
- 原样返回 do_fork() 的负错误码，不重复释放其管理的资源。

Requirements（其他约束）：
- 不在当前线程中直接调用 fn，不执行调度，不修改 current。
- 不将局部 tf 的地址保留到当前调用结束以后供新线程使用。
- 区分新线程 status 中的 SIE 与调用者当前 CSR 的 SIE。
- 保持现有 kernel_thread_entry 返回后调用 do_exit 的框架行为。
```

### 执行结果

- 修改文件：`code/kern/process/proc.c`，在模块二新增 `kernel_thread()`，保留学号 `2412832`，并补充入口寄存器、状态位和共享标志的注释。
- 实现内容：校验非空函数地址，清零临时现场，设置 `s0`、`s1`、`epc` 和 `status`，附加 `CLONE_VM` 后调用 `do_fork()`。
- 验证方式：在 `/tmp/lab4-kernel-thread.u8forspy/code` 临时副本中设置测试父线程与管理结构，构建并运行 QEMU；创建阶段在临时 `copy_mm()` 内记录收到的标志，检查调用参数传递。
- 创建检查：覆盖空函数拒绝、空参数合法、额外 clone 标志保留、默认内核栈、初始现场各字段、调用者中断状态保持，以及进程数已满时的错误码传递。回收创建测试中的线程后，空闲页数恢复基线。
- 入口检查：临时测试将 `current` 指向新线程，通过其 `context.ra` 调用 `forkret`，实际经过 `forkrets -> __trapret -> sret -> kernel_thread_entry` 进入测试函数，确认收到原参数、使用独立内核栈且 SIE 已开启。此处由测试入口发起恢复，调度器的 `switch_to()` 尚未接入。
- 实际结果：临时副本编译成功，QEMU 输出 `KERNEL_THREAD CREATE PASSED` 与 `KERNEL_THREAD ENTRY PASSED`，日志位于对应临时目录的 `build.log` 和 `qemu.log`。测试函数检查完成后直接关闭 QEMU，本轮未测试其返回后的 `do_exit()` 路径。
- 后续步骤：实现 `proc_init()`，初始化管理结构，登记 idleproc 并创建 initproc。

## 模块二——初始化进程管理 proc_init()

### 规范化提示词

```text
[PROMPT]
任务：在当前 Lab4 的 code/kern/process/proc.c 中，
接着已经完成的五个模块二函数，补齐最后的 proc_init()。

功能目标：初始化进程管理结构，将当前启动现场登记为 idleproc，
设置 current，再通过 kernel_thread() 创建第一个内核线程 initproc，
为后续调度做好准备。

修改范围：
- 在 LAB4 function2 2412832 区域实现 proc_init()。
- 保留前五个函数、现有输出及汇编恢复链路，不实现模块三。

操作要求：对照 proc_struct、get_pid()、find_proc()、set_proc_name()
和 test_lab4_process_create()，确认 idle 与普通线程的初始化差异。
添加中文注释，说明启动栈复用、计数、current 设置与创建顺序。

验收方式：在临时工程副本中使用现有测试注入工具，
运行模块一与模块二的检查，并检查名称、链表、哈希、数量和初始现场。
通过临时入口恢复 initproc 的现场，观察 init_main() 输出。
测试辅助入口只放入临时副本，完整调度测试留待模块三接入。

[RELY]
依据当前工程中的真实定义：

1. 全局管理对象：
   proc_list、hash_list、nr_process、idleproc、initproc 和 current。
   hash_list 有 HASH_LIST_SIZE 个桶，每个桶都是链表头。

2. 已有创建接口：
   alloc_proc() 分配并初始化 PCB，默认使用 boot_pgdir_pa。
   kernel_thread() 组织现场，再通过 do_fork() 分配资源和登记线程。
   find_proc() 通过 PID 哈希表查找线程；PID 0 不由该接口查找。

3. idleproc 复用 bootstack，不需要重新分配内核栈或构造初始 tf。
   它计入 nr_process，但不加入普通进程链表和 PID 哈希表。

4. init_main(void *arg) 输出当前 PID、名称和传入字符串。
   本次创建时传入 "Hello world!!"，预期新 PID 为 1。

5. set_proc_name() 固定复制 PROC_NAME_LEN 个字节。
   名称参数应提供足够长度的有效源数组，目标名称末尾由该接口清零。
   可使用 list_init()、panic() 和 assert()。

[GUARANTEE]
必须实现的函数：
void proc_init(void);

辅助函数要求：
复用已有创建、命名和查找接口，不新增正式工程的辅助接口。

[SPECIFICATION]
函数：proc_init()

Pre-Condition（前置条件）：
- 在内核启动阶段调用一次，进程创建尚未开始。
- 物理内存、kmalloc、内核启动页表和启动栈已经可用。
- 系统尚未开始调度，启动阶段中断处于关闭状态。

Post-Condition（后置条件）：
- proc_list 和各 PID 哈希桶完成初始化。
- idleproc 的 PID 为 0、状态为 PROC_RUNNABLE、名称为 idle，
  kstack 指向 bootstack，need_resched 为 1，current 指向 idleproc。
- idleproc 的 parent、mm、tf 为空，runs、flags 和 context 保持初始值。
- initproc 的 PID 为 1、状态为 PROC_RUNNABLE、名称为 init，
  parent 为 idleproc，拥有独立内核栈及完整的首次运行现场。
- nr_process 为 2，普通进程链表和 PID 哈希表仅登记 initproc。
- 两者使用内核页表；initproc 等待后续调度，不在本函数内直接运行。

Case 1（正常初始化）：
- 先建立管理结构，再准备 idleproc 并设置 current，
  最后通过 kernel_thread(init_main, "Hello world!!", 0) 创建 initproc。
- 用返回的 PID 查找 initproc 并设置名称，确认其 PID 为 1。

Case 2（启动资源不足或创建失败）：
- idleproc 分配失败，调用 panic() 终止初始化。
- kernel_thread() 返回错误时，输出错误码并通过 panic() 终止。
- 创建成功但查找结果或预期 PID 异常时，通过断言终止。

Requirements（其他约束）：
- idleproc 不调用 setup_kstack()，不构造尚不存在的首次运行现场。
- 在创建 initproc 前设置 current，保证 do_fork() 建立正确父子关系。
- 命名时保证固定长度复制的源内存有效。
- 保留模块三的预留位置，不补写调度器、上下文切换或进程退出实现。
```

### 执行结果

- 修改文件：`code/kern/process/proc.c`，在模块二新增 `proc_init()`，保留学号 `2412832`。
- 实现内容：初始化链表与哈希桶；创建复用启动栈的 idleproc，设置 current 和初始调度标记；调用 `kernel_thread()` 创建 initproc，再按 PID 查找和命名；启动分配或创建失败时终止初始化。
- 名称处理：由于现有 `set_proc_name()` 按固定长度读取源数据，本次用完整长度的本地字符数组传入 idle 和 init 名称，保持已有命名接口不变。
- 验证方式：在 `/tmp/lab4-proc-init.5xdmu_1r/code` 临时副本中运行原有 `tools/inject.py`，保留 PMM 和进程创建测试，暂不调用调度测试；临时入口进一步检查两个线程的名称、数量、链表、哈希、父子关系、页表和初始现场。
- 实际结果：临时副本编译成功，QEMU 输出 `[Test PMM] PASS`、`[Test Process Create] PASS` 和 `PROC_INIT CHECK PASSED`。
- 首次执行：临时测试通过 initproc 的恢复入口进入 `init_main()`，得到 PID 为 1、名称为 init，以及 `Hello world!!` 和结束语输出；测试副本临时替换 `do_exit()`，检查函数返回值为 0 后输出 `PROC_INIT ENTRY PASSED` 并关闭 QEMU。正式工程的退出接口保持不变。
- 日志位置：对应 WSL 临时目录中的 `build.log` 和 `qemu.log`。
- 后续步骤：模块二六个函数已补齐，等待模块三接入后运行完整 `make qemu`、`make grade`，检查调度与上下文切换。
