# Lab3 提示词记录

本文按成员、任务和轮次汇总 Lab3 提示词及对应结果，分别标注实际发送的输入与事后规范化整理的版本，保留各阶段的实现与验证情况。

## 王雅枫（2412832）—Lab2 物理页初始化迁移

### 提示词（规范化整理）

以下内容原样收录自 [提示词_wyf.md](提示词_wyf.md)，为物理页初始化迁移任务的四段式规范化整理版本。

```text
[PROMPT]
任务：将 Lab2 中已实现的物理页初始化功能迁移到 Lab3。

功能目标：复用 Lab2 的 page_init()，根据设备树提供的物理内存信息，
建立 Page 描述符数组，保留固件、内核和描述符数组占用的页面，
并将剩余完整页面注册到当前物理内存管理器。

修改范围：
- 在 lab3_new/kern/mm/pmm.c 的 Lab2 预留位置补入 page_init()。
- 允许在 lab3_new/kern/mm/memlayout.h 中补充必要的地址宏。
- 保留 Lab3 的中断保护、原子位操作、现有接口和管理器选择。
- Best-Fit 由其他成员负责，本次不迁移该分配器。
- 保留任务注释和学号 2412832。

操作要求：先对比两个实验的接口和内存布局，再迁移实际代码。
完成后检查差异并编译相关文件，说明验证结果和未验证部分。


[RELY]
依据当前工程中的真实定义：

1. 迁移来源：
   lab2 分支中的 lab2_new/kern/mm/pmm.c。

2. 设备树接口：
   uint64_t get_memory_base(void);
   uint64_t get_memory_size(void);
   kern_init() 已在 pmm_init() 之前调用 dtb_init()。

3. 地址及页面相关定义：
   KERNBASE、KERNTOP、PHYSICAL_MEMORY_OFFSET、
   DRAM_BASE、PGSIZE、PPN、ROUNDUP、ROUNDDOWN 和 PADDR。

   Lab3 尚未定义旧实现依赖的：
   KERNEL_BEGIN_PADDR、PHYSICAL_MEMORY_END。
   应根据当前虚拟地址布局和虚实偏移补齐。

4. 数据结构和接口：
   struct Page 包含 ref、flags、property 和 page_link。
   可使用 set_page_ref()、SetPageReserved()、list_init()、
   pa2page()、init_memmap()、panic() 和 cprintf()。

5. 全局变量含义：
   va_pa_offset：内核直接映射的虚拟地址减物理地址。
   pages：Page 描述符数组的虚拟地址。
   nbase：DRAM 起始物理页号。
   npage：可管理物理页号的排他上界，
          描述符数量为 npage - nbase。

6. Lab3 的 alloc_pages()、free_pages() 和 nr_free_pages()
   已包含 local_intr_save()/local_intr_restore()，需要保留。


[GUARANTEE]
必须迁移的函数：
static void page_init(void);

必要的配套修改：
补齐 KERNEL_BEGIN_PADDR 和 PHYSICAL_MEMORY_END 的定义，
使其与当前 KERNBASE、KERNTOP 和 PHYSICAL_MEMORY_OFFSET 一致。

辅助函数要求：
优先复用已有接口，本任务不需要新增对外接口。


[SPECIFICATION]
函数：page_init()

Pre-Condition（前置条件）：
- 设备树初始化流程已经执行，但仍需校验读取到的内存信息。
- 当前物理内存管理器已完成 init()。
- 启动页表已建立内核直接映射。
- 链接符号 end 表示当前内核镜像的结束位置。

Post-Condition（后置条件）：
- 正确设置 va_pa_offset、npage 和 pages。
- 描述符数组位于内核镜像之后，且完全落在可管理内存内。
- 描述符初始化时具有确定的引用计数、标志、property 和链表状态。
- 固件、内核、描述符数组及不足一页的边界空间不进入空闲区。
- 合法的完整空闲页通过 init_memmap() 注册给当前管理器。

Case 1（正常情况）：
- 内存信息合法且空间充足。
- 根据当前内核 end 动态计算描述符位置和空闲页范围。
- 输出探测范围、管理上界、描述符位置和空闲页数量。

Case 2（异常与边界情况）：
- 内存信息为空、地址运算溢出、内存无法覆盖内核、
  描述符空间不足或没有完整空闲页时，通过 panic() 终止。
- 所有容量和溢出检查必须先于描述符数组写入。
- 不得以零页面数量调用 init_memmap()。

Requirements（其他约束）：
- 在首次使用 PADDR() 前设置 va_pa_offset。
- 正确区分虚拟地址、物理地址、物理页号和描述符地址。
- 管理上界向下按页对齐，空闲起点向上按页对齐。
- 保留 Lab2 实现已有的安全检查。
- 使用 Lab3 编译参数验证 pmm.c。
- 若其他未完成模块妨碍整体编译或运行，应明确说明，
  不得把单文件编译成功描述为整个 Lab3 测试通过。
```

### 对应实现结果

- `lab3_new/kern/mm/pmm.c`：迁入 Lab2 的 `page_init()`，保留学号 `2412832` 和 Lab3 的中断保护接口。
- `lab3_new/kern/mm/memlayout.h`：根据直接映射关系补充 `KERNEL_BEGIN_PADDR` 和 `PHYSICAL_MEMORY_END`。
- 验证结果：`pmm.c` 使用 Lab3 编译参数完成单文件编译；当时保留框架的管理器选择，由队友后续接入 Best-Fit。

---

## 胡楚骅（2413276）—模块一第 1 轮：Trap 入口调用

### 提示词原文

```text
[PROMPT]

任务：

在 WSL 工程 `/home/sven1/course-project/os/OS-/lab3_new` 中，补全 `kern/trap/trapentry.S` 的 `__alltraps` 标签下、`SAVE_ALL` 之后的待填部分，完成向 C 函数 `trap()` 传递现场并调用的功能。

操作要求：

先检查分支、工作区和相关源码，保留已有修改。实际修改上述待填位置，将该处 `YOUR CODE` 替换为学号 `2413276`。不修改其他代码、报告和提示词记录，不提交或推送。

输出与验收要求：

解释新增指令，并使用现有构建入口进行必要验证。若其他未完成部分阻碍验证，如实说明，不擅自补写其他模块。

[RELY]

`SAVE_ALL` 已在栈上建立完整的 Trap frame，当前 `sp` 指向其起始地址，原来的通用寄存器值均已保存。现场布局由 `kern/trap/trap.h` 中的 `struct trapframe` 定义。

C 函数接口为 `void trap(struct trapframe *tf)`。RISC-V 调用约定使用 `a0` 传递第一个参数，使用 `ra` 保存函数返回地址。待填部分之后已有 `__trapret`、`RESTORE_ALL` 和 `sret` 返回框架。

[GUARANTEE]

补全 `__alltraps` 中调用 `trap()` 的逻辑，正确传递现场地址并衔接返回流程。不改变已有接口、现场布局和 `SAVE_ALL`，不在本轮实现 `RESTORE_ALL`。

[SPECIFICATION]

功能：`__alltraps` 中调用 `trap()` 的部分。

Pre-Condition：

`SAVE_ALL` 已完成，`sp` 指向有效的 Trap frame，且满足调用约定的栈对齐要求。

Post-Condition：

`trap()` 接收的 `tf` 等于调用前的 `sp`；函数正常返回后，`sp` 与调用前一致，执行流继续进入 `__trapret`。

Case 1（正常情况）：

将当前现场地址作为第一个参数调用 `trap()`，处理结束后返回已有恢复流程。

Requirements：

正确建立函数返回地址，不破坏已保存的现场，不提前恢复旧 `sp`，保留 C 处理函数对 Trap frame 字段的合法修改。
```

### Codex 回复原文

已在分支 `lab3` 完成修改；初始工作区干净，仅改动 [trapentry.S (line 68)](/home/sven1/course-project/os/OS-/lab3_new/kern/trap/trapentry.S:68)：
```bash
    #lab3 function1 2413276
    mv a0, sp
    jal trap
```

- `mv a0, sp`：将现场起始地址作为 `trap(tf)` 的第一个参数。
- `jal trap`：将返回地址写入 `ra` 并调用 `trap`，正常返回后顺接 `__trapret`。

新增指令不调整 `sp`、不写入已保存的现场，保留 C 函数对现场字段的修改；`trap()` 按调用约定返回时恢复调用前的 `sp`。

验证结果：

- `git diff --check` 通过。
- `make` 因现有 `interrupt_handler`、`exception_handler` 缺少声明而失败。
- 单独构建 `trapentry.o` 因汇编器不识别原有 `SAVE_ALL` 中的 `sbadaddr` 而失败，因此未完成运行验证。

未补写其他模块或 `RESTORE_ALL`，未修改报告和提示词记录。构建产物已清理，未提交或推送。

## 胡楚骅（2413276）—模块一第 2 轮：RESTORE_ALL 现场恢复

### 提示词原文

```text
[PROMPT]
任务：
在 WSL 工程 `/home/sven1/course-project/os/OS-/lab3_new` 中，补全 `kern/trap/trapentry.S` 的 `RESTORE_ALL` 汇编宏，使 Trap 处理完成后能够恢复现场，衔接已有的 `sret` 返回。
操作要求：
先检查分支、工作区和相关源码，保留已有修改。实际修改该宏的待填部分，将对应 `YOUR CODE` 替换为学号 `2413276`。不修改其他代码、报告和提示词记录，不提交或推送。
输出与验收要求：
解释恢复顺序及关键指令，使用现有构建和运行入口进行必要验证。若其他未完成部分阻碍验证，如实说明，不擅自补写其他模块，不将未验证的结果写成通过。
[RELY]
`SAVE_ALL` 已按 `struct trapframe` 的布局保存现场。RV64 下每个槽位为 8 字节，通用寄存器对应编号为 0～31 的槽位，旧 `sp` 位于槽位 2，`sstatus`、`sepc`、`sbadaddr` 和 `scause` 依次位于槽位 32～35。
已有入口调用 `trap(tf)`，正常返回后进入 `__trapret`，执行 `RESTORE_ALL` 和 `sret`。`riscv.h` 提供 `LOAD` 和 `REGBYTES` 等宏，具体定义以当前源码为准。
[GUARANTEE]
实现与现有 `SAVE_ALL` 和 Trap frame 布局对应的 `RESTORE_ALL`。恢复返回所需的 CSR 和通用寄存器，不改变结构体、保存逻辑、已有入口调用及 `sret`。
[SPECIFICATION]
功能：`RESTORE_ALL`。
Pre-Condition：
`trap()` 已正常返回，当前 `sp` 指向有效的 Trap frame，其中字段为处理完成后应采用的最终现场。
Post-Condition：
`sstatus` 和 `sepc` 恢复为 Trap frame 中的对应值，除恒为零的 `x0` 外，通用寄存器恢复为对应字段的值，`sp` 恢复为保存的旧栈指针，随后可执行已有的 `sret`。
Case 1（正常返回）：
处理函数未调整返回现场时，恢复保存的寄存器与状态，使被打断的执行流能够继续。
Case 2（处理函数调整现场）：
处理函数修改了 `epc` 等字段时，恢复修改后的值，不擅自撤销调整，也不在恢复宏中额外增加返回地址偏移。
Requirements：
访问 CSR 时通过通用寄存器中转，先完成中转操作再恢复所借用寄存器的最终值。保持当前 `sp` 作为现场读取基准，最后恢复旧 `sp`。不必将用于诊断的 `cause` 和 `badvaddr` 写回 CSR，不额外开启中断或改变返回特权级。
```

### Codex 回复原文

已在 `lab3` 分支补全 [trapentry.S (line 57)](/home/sven1/course-project/os/OS-/lab3_new/kern/trap/trapentry.S:57) 的 `RESTORE_ALL`，对应 `YOUR CODE` 已替换为 `2413276`。仅修改该宏，保留已有入口调用及其他代码。

恢复顺序：

- 用 `LOAD`（RV64 下为 `ld`）将槽位 32、33 读入 `s0/s1`，再用 `csrw` 恢复 `sstatus/sepc`。
- 按对应槽位恢复 `x1`、`x3`～`x31`，其中包含借用的 `s0/s1` 的最终值；`x0` 恒为零，无需恢复。
- 最后从槽位 2 恢复 `sp`，随后执行已有 `sret`。

宏直接采用 Trap frame 中的最终值，不额外调整 `epc`，不回写诊断 CSR，也不额外开启中断或修改返回特权级。

验证结果：

- `git diff --check` 通过。
- `make`、`make qemu` 均因 `trap.c` 中 `interrupt_handler`、`exception_handler` 未声明而失败，QEMU 未启动。
- 单独构建 `trapentry.o` 被原有 `SAVE_ALL` 中的 `unknown CSR 'sbadaddr'` 错误阻断。

因此尚未完成编译及运行验证。未补写其他模块，构建产物已清理，未提交或推送。

## 罗元伟（2412737）—模块二：中断与异常处理

### 规范化练习 第 1 轮：只读分析，确认接口与验收标准

设计思路：模块二要和模块一的 Trap 返回流程、时钟驱动和评测脚本同时衔接。先让 AI 只读不写，把真实接口、评测检查项和框架缺失项列出来，后续提示词的 `[RELY]` 就能完全基于源码，而不是基于 AI 的猜测。

```text
[PROMPT]
任务：分析 lab3_new 中功能模块二（kern/trap/trap.c 的 interrupt_handler()、exception_handler()）的实现条件。本轮只读，不修改任何文件。
操作要求：
1. 阅读 kern/trap/trap.c、kern/trap/trap.h、kern/driver/clock.c/.h、libs/riscv.h、libs/sbi.h，列出模块二可用的真实宏、结构体字段、全局变量和函数签名。
2. 阅读 tools/grade.sh、tools/test_lab3.c、tools/test_lab3_context.S、tools/inject.py，说明评测如何构造测试、检查哪些输出字符串、各项分值和超时。
3. 指出指导书提到但框架中不存在的符号，以及当前 make 失败的原因。
4. 检查评测用例触发异常的指令在本工具链下的实际编码长度。
输出要求：分条列出结论并给出依据（文件与行号），不要给出实现代码。
```

执行结果：

- 当前 `make` 因 `interrupt_handler`、`exception_handler` 隐式声明在 `-Werror` 下失败。
- `TICK_NUM`（100）和 `print_ticks()` 已在 `trap.c` 中定义，`ticks` 为 `clock.c` 中的 `volatile size_t`；指导书提到的 `SHUTDOWN_AFTER` 在源码中不存在，需要自行定义。
- 评测检查 `Breakpoint at`、`Illegal instruction at`、`Supervisor software interrupt`、`Unknown interrupt: cause 15`、`cause    0x00000014`、`100 ticks` 与 `ticks=200`，三项分值为 35/25/40。
- `test_lab3.c` 用 `extern` 直接调用两个处理函数，因此它们不能声明为 `static`。
- 工具链默认带 C 扩展，`ebreak` 会被汇编为 2 字节的 `c.ebreak`，评测中的非法指令 `.word 0x7b` 为 4 字节，`epc` 推进量不能固定为 4。

---

### 规范化练习 第 2 轮：四段式提示词，实现两个处理函数

设计思路：把第 1 轮的结论写入 `[RELY]`；在 `[SPECIFICATION]` 中按“时钟中断 / 其他中断 / 规定异常 / 其他异常”分 Case 描述行为，重点约束三处容易出错的地方：时钟必须重设、`epc` 按指令长度推进、不得改动规定以外的函数。

```text
[PROMPT]
任务：在 lab3_new/kern/trap/trap.c 中 "//lab3 function2 YOUR CODE" 处实现 interrupt_handler() 和 exception_handler()，并将 YOUR CODE 替换为学号 2412737。
功能目标：时钟中断能够持续产生并按规定输出和关机；非法指令与断点异常输出规定信息后返回下一条指令；其他中断和异常有类型提示或兜底输出。
修改范围：只在模块二标注处新增代码，可在文件头补充必要的 #include；不修改 trap_dispatch()、trap()、print_trapframe() 等其他函数和任何函数签名，不修改模块一汇编与测试文件。
操作要求：修改实际文件，为新增代码编写中文注释，完成后执行 make 确认编译通过。

[RELY]
依据当前工程中的真实定义：
- struct trapframe { struct pushregs gpr; uintptr_t status; uintptr_t epc; uintptr_t badvaddr; uintptr_t cause; };
- trap_dispatch() 以 (intptr_t)tf->cause < 0 判断中断，否则为异常；处理函数返回后由 __trapret 中的 RESTORE_ALL 把 tf->epc 写回 sepc 再执行 sret。
- libs/riscv.h：IRQ_U_SOFT 0、IRQ_S_SOFT 1、IRQ_H_SOFT 2、IRQ_M_SOFT 3、IRQ_U_TIMER 4、IRQ_S_TIMER 5、IRQ_H_TIMER 6、IRQ_M_TIMER 7、IRQ_U_EXT 8、IRQ_S_EXT 9、IRQ_H_EXT 10、IRQ_M_EXT 11；CAUSE_ILLEGAL_INSTRUCTION 0x2、CAUSE_BREAKPOINT 0x3。
- kern/driver/clock.h：extern volatile size_t ticks; void clock_set_next_event(void);（通过 sbi_set_timer 设置一次性比较值，不会自动重复）
- trap.c 已有：#define TICK_NUM 100；static void print_ticks(void)（输出 "100 ticks"）；void print_trapframe(struct trapframe *tf)。
- libs/sbi.h：void sbi_shutdown(void);
- 框架未定义 SHUTDOWN_AFTER，需要在模块二位置自行定义。
- 评测通过 extern 直接调用两个处理函数，并检查输出中的 "Breakpoint at"、"Illegal instruction at"、"Supervisor software interrupt"、"Unknown interrupt: cause 15"。
- 工具链启用 C 扩展：ebreak 被汇编为 2 字节的 c.ebreak，评测中的非法指令为 4 字节。

[GUARANTEE]
必须实现的函数：
void interrupt_handler(struct trapframe *tf);
void exception_handler(struct trapframe *tf);
辅助函数要求：允许新增 static 辅助函数（如计算指令长度），不得新增对外接口；两个处理函数保持非 static。

[SPECIFICATION]
函数：interrupt_handler()
Pre-Condition：tf 指向有效的 Trap frame，tf->cause 最高位为 1（评测也可能直接传入未置最高位的编号）。
Post-Condition：除时钟中断更新 ticks 和下一次定时外，不修改 tf 中的现场字段，正常返回。
Case 1（IRQ_S_TIMER）：先重设下一次时钟事件，再使 ticks 加一；ticks 为 TICK_NUM 的整数倍时调用 print_ticks()；输出次数达到 SHUTDOWN_AFTER 时调用 sbi_shutdown()。
Case 2（其他已知中断）：按 U/S/H/M 输出类型，例如 "Supervisor software interrupt"。
Case 3（无法识别的中断）：输出 "Unknown interrupt: cause N"，并调用 print_trapframe(tf)。
Requirements：取中断编号时去掉 cause 最高位；不能只计数不重设定时器。

函数：exception_handler()
Pre-Condition：tf 指向有效的 Trap frame，tf->epc 指向触发异常的指令。
Post-Condition：规定异常处理后 tf->epc 指向下一条指令；其他异常不修改 tf->epc。
Case 1（CAUSE_ILLEGAL_INSTRUCTION）：输出异常类型与 "Illegal instruction at 0x<epc>"，epc 加上该指令长度。
Case 2（CAUSE_BREAKPOINT）：输出异常类型与 "Breakpoint at 0x<epc>"，epc 加上该指令长度。
Case 3（其他异常）：调用 print_trapframe(tf) 兜底。
Requirements：指令长度按 RISC-V 编码规则判断——读取 epc 处低 16 位，最低两位为 0b11 时为 4 字节，否则为 2 字节；不得统一加 4；不依赖 tf->badvaddr 获取指令内容（规范允许其为 0）。
```

执行结果：

- 修改的文件及主要内容：`trap.c` 新增 `SHUTDOWN_AFTER`（取 10）、`num_prints`、`trap_insn_len()` 与两个处理函数，并加入 `#include <sbi.h>`。
- 编译或测试命令：`make`。
- 实际结果：编译、链接通过，`-Werror` 下无警告。
- 发现的问题与下一步：尚未运行验证，进入第 3 轮。

---

### 规范化练习 第 3 轮：运行验证，区分环境问题与代码问题

设计思路：要求 AI 用真实命令验证并给出证据；运行失败时先判断是环境还是代码问题，避免为了“让测试通过”去改动框架或评测文件。

```text
[PROMPT]
任务：验证功能模块二。
操作要求：
1. 依次运行 make qemu 与 make grade，记录关键输出与得分。
2. 若运行失败，先判断是环境问题还是代码问题并给出依据；不得修改 tools/ 下的评测文件、Makefile 或模块一代码来规避失败。
3. 用 objdump 确认评测内核中 ebreak 与非法指令的实际编码长度，并与输出中的地址对应，证明 epc 推进量正确。
输出要求：区分“已验证通过”和“未验证”的内容，不把单项编译成功描述为整体通过。
```

执行结果：

- 实际结果：系统自带的 QEMU 8.2.2（新版 OpenSBI）没有跳转到 `0x80200000` 处的内核，30 秒内无内核输出，判断为环境问题，代码未做改动；改用 QEMU 4.1.1（OpenSBI v0.4）后 `make qemu` 输出 10 行 `100 ticks` 并自动关机。`tools/grade.sh` 调用 `PATH` 中的 `qemu-system-riscv64`，因此将 4.1.1 放在 `PATH` 最前后运行 `make grade`，结果为 35/35、25/25、40/40，总分 100/100。
- 编码证据：评测内核中 `ffffffffc0201b50: 9002 ebreak`（2 字节）与 `ffffffffc0201b00: 0000007b .word 0x0000007b`（4 字节），与输出中的 `Breakpoint at 0xffffffffc0201b50`、`Illegal instruction at 0xffffffffc0201b00` 一致。
- 发现的问题与下一步：无代码问题；环境要求写入实现说明。

---

### 规范化练习 第 4 轮：核对注释与事实一致（失败后修正）

设计思路：AI 生成的注释可能把自行设定的取值写成“文档规定”。提交前要求逐条核对注释中的事实依据。

```text
[PROMPT]
任务：检查 trap.c 模块二新增的中文注释。
操作要求：逐条核对注释中关于“指导书要求”“框架提供”的表述是否有源码或指导书原文依据；没有依据的改为如实描述，不改变代码逻辑。修改后重新编译。
```

执行结果：

- 发现的问题：`SHUTDOWN_AFTER` 的注释写成“按指导书要求取 10”，但指导书只要求累计输出后关机，没有给出次数。
- 修正：改为“框架和指导书都没有给出具体数值，这里取 10：每秒输出一行，约 10 秒后关机”，重新编译通过，并在推送前合入代码提交。

---

### 规范化练习 第 5 轮：整理实现说明并分两次推送

```text
[PROMPT]
任务：为功能模块二撰写实现说明并提交。
操作要求：
1. 新建 report/模块二实现说明_lyw.md，说明函数在 Trap 流程中的位置、两个处理函数的行为、epc 推进依据与验证结果，引用真实输出与反汇编证据。
2. 保存 make qemu 与 make grade 的终端截图到 report/images/，在报告中以相对路径引用。
3. 分两次推送：第一次只含代码，第二次含说明、截图与报告更新；推送前检查差异，不提交构建产物和本地环境修改。
```

执行结果：

- 第一次推送 `a599ff2`：模块二代码与 `.gitignore`。
- 第二次推送 `034035d`：`report/模块二实现说明_lyw.md`、`report/images/lab3_make_qemu.png`、`report/images/lab3_make_grade.png` 与报告更新。
