# Lab3 功能模块二实现说明：中断与异常处理

**负责人：** 2412737-罗元伟

**涉及文件：** `lab3_new/kern/trap/trap.c`（`//lab3 function2 2412737` 标注处）

**实现的函数：**

```c
void interrupt_handler(struct trapframe *tf);    // 中断处理
void exception_handler(struct trapframe *tf);    // 异常处理
static size_t trap_insn_len(uintptr_t epc);      // 辅助函数：计算 epc 处指令长度
```

此外在同一位置补充了 `SHUTDOWN_AFTER` 宏和计数变量 `num_prints`，并在文件头加入 `#include <sbi.h>` 以调用 `sbi_shutdown()`。没有修改 `trap_dispatch()`、`trap()`、`print_trapframe()` 等其他函数及其签名。

---

## 1. 在 Trap 流程中的位置

```text
硬件 Trap → __alltraps（SAVE_ALL，构造 trapframe）→ trap(tf) → trap_dispatch(tf)
    ├─ (intptr_t)tf->cause < 0（最高位为 1）→ interrupt_handler(tf)
    └─ 否则                                 → exception_handler(tf)
→ 返回 __trapret（RESTORE_ALL，把可能被修改的 tf->epc 写回 sepc）→ sret
```

模块二只读写 `tf` 中的字段，处理完后正常返回。对返回位置的调整全部通过修改 `tf->epc` 完成，再由模块一的 `RESTORE_ALL` 写回 `sepc`，因此两个模块之间的约定是：**Trap frame 中的值就是返回后应采用的最终现场**。

---

## 2. `interrupt_handler()`：中断处理

### 2.1 取出中断编号

中断的 `scause` 最高位为 1，低位才是中断编号。实现中用 `(tf->cause << 1) >> 1` 清掉最高位，再按 `riscv.h` 中的 `IRQ_*` 宏分类。

### 2.2 S 态时钟中断（`IRQ_S_TIMER`）

```c
clock_set_next_event();              // 1. 先重设下一次时钟事件
if (++ticks % TICK_NUM == 0) {       // 2. 计数，每 100 次输出一行
    print_ticks();                   //    输出 "100 ticks"
    if (++num_prints == SHUTDOWN_AFTER) {
        sbi_shutdown();              // 3. 累计输出 10 行后关机
    }
}
```

- **为什么必须重设：** `clock_set_next_event()` 通过 `sbi_set_timer()` 把“当前时间 + timebase”交给 M 态固件，这只是一次性的比较值。不在中断里重新设置，就只会收到第一次时钟中断。评测 Test 3 要求 `ticks` 达到 200，专门检查这一点。
- **为什么先重设再计数：** 下一次中断的时刻以“现在”为基准，先设置可以让间隔尽量接近 `timebase`（QEMU 10 MHz 下 100000 个节拍，即 10 ms），不受后面 `cprintf` 输出耗时的影响。
- **`ticks`：** 定义在 `clock.c` 中，为 `volatile size_t`，主循环（评测中的 `test_lab3_wait_for_timer()`）会轮询它，`volatile` 保证每次都从内存读取。
- **`SHUTDOWN_AFTER`：** 框架与指导书都没有给出该宏的定义和取值，这里定义为 10，即每秒输出一行 `100 ticks`，约 10 秒后关机。这样 `make qemu` 运行一段时间后会自动退出，同时评测 Test 3 在第 2 行之后就已判定通过，不受关机影响。

### 2.3 其他中断

| 中断编号 | 处理 |
|---|---|
| `IRQ_U/S/H/M_SOFT` | 输出 `User / Supervisor / Hypervisor / Machine software interrupt` |
| `IRQ_U/H/M_TIMER` | 输出对应的 `... timer interrupt` |
| `IRQ_U/S/H/M_EXT` | 输出对应的 `... external interrupt` |
| 其他编号 | 输出 `Unknown interrupt: cause N`，并调用 `print_trapframe(tf)` 打印完整现场 |

评测 Test 2 会直接构造 `cause = IRQ_S_SOFT | (1 << 63)` 和 `cause = 15` 两个 Trap frame 调用本函数，分别检查 `Supervisor software interrupt` 与 `Unknown interrupt: cause 15` 两行输出。

---

## 3. `exception_handler()`：异常处理

| 异常 | 输出 | `epc` 处理 |
|---|---|---|
| `CAUSE_ILLEGAL_INSTRUCTION` | `Exception type: Illegal instruction`、`Illegal instruction at 0x...` | 加上该指令长度 |
| `CAUSE_BREAKPOINT` | `Exception type: Breakpoint`、`Breakpoint at 0x...` | 加上该指令长度 |
| 其他 | `print_trapframe(tf)` | 不修改 |

### 3.1 为什么必须推进 `epc`

异常是同步事件，`sepc` 指向的是**触发异常的那条指令本身**。如果不修改 `tf->epc`，`sret` 后会再次执行同一条 `ebreak` 或非法指令，立即再次陷入，形成异常循环。

### 3.2 为什么不能统一加 4

工具链默认使用 `rv64gc`，带 C（压缩指令）扩展，汇编器会把 `ebreak` 压缩为 2 字节的 `c.ebreak`。从评测内核的反汇编可以看到两种长度同时存在：

```text
ffffffffc0201b50:	9002          	ebreak            # 评测 Test 1：压缩编码，2 字节
ffffffffc0201b00:	0000007b      	.word	0x0000007b  # 评测 Test 2：非法指令，4 字节
```

如果统一加 4，Test 1 中返回地址会落到下一条指令的中间，后续寄存器检查必然失败。

`trap_insn_len()` 按 RISC-V 指令编码规则判断长度：读取 `epc` 处的低 16 位，最低两位为 `0b11` 表示 32 位标准指令，否则为 16 位压缩指令。

```c
static size_t trap_insn_len(uintptr_t epc) {
    uint16_t low = *(const uint16_t *)epc;
    return ((low & 0x3) == 0x3) ? 4 : 2;
}
```

- 只读 16 位：压缩指令只保证 2 字节对齐，按 32 位读取可能出现非对齐访问或越过有效指令。
- 不使用 `tf->badvaddr`（`stval`）：RISC-V 规范允许实现在非法指令异常时把 `stval` 置 0，依赖它不可靠；直接读指令内存与具体实现无关。
- 48 位、64 位等更长的编码在 RV64GC 上没有定义，这里不处理。

### 3.3 其他异常

本实验不要求恢复访存错误、`ecall` 等异常，统一用 `print_trapframe(tf)` 兜底且不修改 `epc`。评测 Test 2 用 `cause = 20` 调用本函数，检查输出中的 `cause    0x00000014`。

---

## 4. 验证

环境：WSL2（Ubuntu），`riscv64-unknown-elf-gcc 13.2.0`，QEMU 4.1.1（OpenSBI v0.4）。

| 检查 | 结果 |
|---|---|
| `make` | 编译、链接通过（`-Werror` 下无警告） |
| `make qemu` | 依次完成 DTB 解析、Best-Fit 物理内存初始化与 `check_alloc_page()` 自检，开启时钟中断后每秒输出一行 `100 ticks`，共 10 行后自动关机 |
| `make grade` | Test 1 断点与上下文恢复 35/35，Test 2 非法指令与兜底输出 25/25，Test 3 持续时钟中断 40/40，**总分 100/100** |

截图见 `report/images/lab3_make_qemu.png` 和 `report/images/lab3_make_grade.png`。

评测中两个异常用例的实际输出（摘录）：

```text
Exception type: Breakpoint
Breakpoint at 0xffffffffc0201b50
[Test 1] Trap entry and breakpoint return: PASS (35/35)

Exception type: Illegal instruction
Illegal instruction at 0xffffffffc0201b00
Supervisor software interrupt
Unknown interrupt: cause 15
trapframe at 0xffffffffc0205eb0
...
```

**运行环境注意：** `tools/grade.sh` 直接调用 `PATH` 中的 `qemu-system-riscv64`。本机系统自带的 QEMU 8.2.2 搭配其新版 OpenSBI 时没有跳转到 `0x80200000` 处的内核，`make qemu` 无输出，因此验证时把 QEMU 4.1.1 放在 `PATH` 最前：

```bash
export PATH=/path/to/qemu-4.1.1/riscv64-softmmu:$PATH
make qemu
make grade
```
