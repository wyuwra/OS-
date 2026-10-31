# Lab3 提示词记录

记录说明：两轮提示词及 Codex 回复由用户提供的实现会话记录整理，仅调整换行、移除界面操作文字。验证结果来自实现会话，本次整理未重新运行；各轮状态为当轮历史记录。

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
