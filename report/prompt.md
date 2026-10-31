# Lab3 提示词记录

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

### 当前代码状态

本次只读核对发现，`trapentry.S` 的对应位置已新增 `mv a0, sp` 和 `jal trap`，学号标记为 `2413276`。`RESTORE_ALL` 仍待实现。本次未运行编译或实验，未核实其他会话中的验证结果，不将当前状态记为完整 Trap 流程通过。
