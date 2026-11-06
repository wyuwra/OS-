#include <assert.h>
#include <clock.h>
#include <console.h>
#include <defs.h>
#include <kdebug.h>
#include <memlayout.h>
#include <mmu.h>
#include <riscv.h>
#include <sbi.h> /* sbi_shutdown()：时钟中断累计输出后关机 */
#include <stdio.h>
#include <trap.h>

/* TICK_NUM - 每打印一次 "ticks" 信息对应的时钟中断次数 */
#define TICK_NUM 100

/* print_ticks - 打印时钟节拍信息；开启 DEBUG_GRADE 时输出结束标记并 panic 退出评测 */
static void print_ticks()
{
    cprintf("%d ticks\n", TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

/* idt_init - 初始化陷入向量：将 stvec 指向汇编入口 __alltraps
 * 同时把 sscratch 清零，告诉入口代码当前正在内核态执行。 */
void idt_init(void)
{

    extern void __alltraps(void);
    /* Set sup0 scratch register to 0, indicating to exception vector
       that we are presently executing in the kernel */
    /* 清零 sscratch：0 表示当前位于内核态 */
    write_csr(sscratch, 0);
    /* Set the exception vector address */
    /* 设置陷入向量基址，之后所有中断/异常都会跳到 __alltraps */
    write_csr(stvec, &__alltraps);
}

/* trap_in_kernel - 判断陷入发生时是否处于内核态（sstatus.SPP 为 1 表示陷入前为 S 态） */
bool trap_in_kernel(struct trapframe *tf)
{
    return (tf->status & SSTATUS_SPP) != 0;
}

/* print_trapframe - 打印陷入现场：trapframe 地址、通用寄存器及 status/epc/badvaddr/cause */
void print_trapframe(struct trapframe *tf)
{
    cprintf("trapframe at %p\n", tf);
    print_regs(&tf->gpr);
    cprintf("  status   0x%08x\n", tf->status);
    cprintf("  epc      0x%08x\n", tf->epc);
    cprintf("  badvaddr 0x%08x\n", tf->badvaddr);
    cprintf("  cause    0x%08x\n", tf->cause);
}

/* print_regs - 逐个打印保存在 pushregs 中的通用寄存器值 */
void print_regs(struct pushregs *gpr)
{
    cprintf("  zero     0x%08x\n", gpr->zero);
    cprintf("  ra       0x%08x\n", gpr->ra);
    cprintf("  sp       0x%08x\n", gpr->sp);
    cprintf("  gp       0x%08x\n", gpr->gp);
    cprintf("  tp       0x%08x\n", gpr->tp);
    cprintf("  t0       0x%08x\n", gpr->t0);
    cprintf("  t1       0x%08x\n", gpr->t1);
    cprintf("  t2       0x%08x\n", gpr->t2);
    cprintf("  s0       0x%08x\n", gpr->s0);
    cprintf("  s1       0x%08x\n", gpr->s1);
    cprintf("  a0       0x%08x\n", gpr->a0);
    cprintf("  a1       0x%08x\n", gpr->a1);
    cprintf("  a2       0x%08x\n", gpr->a2);
    cprintf("  a3       0x%08x\n", gpr->a3);
    cprintf("  a4       0x%08x\n", gpr->a4);
    cprintf("  a5       0x%08x\n", gpr->a5);
    cprintf("  a6       0x%08x\n", gpr->a6);
    cprintf("  a7       0x%08x\n", gpr->a7);
    cprintf("  s2       0x%08x\n", gpr->s2);
    cprintf("  s3       0x%08x\n", gpr->s3);
    cprintf("  s4       0x%08x\n", gpr->s4);
    cprintf("  s5       0x%08x\n", gpr->s5);
    cprintf("  s6       0x%08x\n", gpr->s6);
    cprintf("  s7       0x%08x\n", gpr->s7);
    cprintf("  s8       0x%08x\n", gpr->s8);
    cprintf("  s9       0x%08x\n", gpr->s9);
    cprintf("  s10      0x%08x\n", gpr->s10);
    cprintf("  s11      0x%08x\n", gpr->s11);
    cprintf("  t3       0x%08x\n", gpr->t3);
    cprintf("  t4       0x%08x\n", gpr->t4);
    cprintf("  t5       0x%08x\n", gpr->t5);
    cprintf("  t6       0x%08x\n", gpr->t6);
}

// lab3 function2 2412737

/* SHUTDOWN_AFTER - 输出多少次 "100 ticks" 后关机
 * 框架和指导书都没有给出具体数值，这里我爱设多少都行，阿哈：每秒输出一行，
 * 约 10 秒（1000 次时钟中断）后调用 sbi_shutdown() 结束 QEMU。 */
#define SHUTDOWN_AFTER 40

/* num_prints - 已经输出 "100 ticks" 的次数，只在时钟中断处理中修改 */
static int num_prints = 0;

/* trap_insn_len - 计算 epc 处那条指令的字节长度
 * RISC-V 指令长度由编码的最低几位决定：
 *   - 最低两位不为 0b11：16 位压缩指令（C 扩展，例如 c.ebreak，2 字节）；
 *   - 最低两位为 0b11：标准 32 位指令（例如 ebreak、非法的 .word 0x7b，4 字节）。
 * 48/64 位等更长的编码在 RV64GC 上未定义，这里不考虑。
 * 由于 C 扩展只保证 2 字节对齐，按 16 位读取低半字，避免越界或非对齐访问。
 * 不使用 tf->badvaddr(stval)：规范允许非法指令异常时 stval 为 0，不可靠。 */
static size_t trap_insn_len(uintptr_t epc)
{
    uint16_t low = *(const uint16_t *)epc;
    return ((low & 0x3) == 0x3) ? 4 : 2;
}

/* interrupt_handler - 处理中断（scause 最高位为 1）
 * 先去掉 scause 的最高位得到中断编号，再按编号分类：
 *   - S 态时钟中断：重设下一次时钟事件、计数、按 TICK_NUM 输出、累计后关机；
 *   - 其他已知中断：只输出类型提示；
 *   - 无法识别的中断：输出编号并打印完整 trapframe 便于调试。 */
void interrupt_handler(struct trapframe *tf)
{
    /* 左移一位再右移一位，清掉表示“中断”的最高位，得到中断编号 */
    intptr_t cause = (tf->cause << 1) >> 1;
    switch (cause)
    {
    case IRQ_U_SOFT:
        cprintf("User software interrupt\n");
        break;
    case IRQ_S_SOFT:
        cprintf("Supervisor software interrupt\n");
        break;
    case IRQ_H_SOFT:
        cprintf("Hypervisor software interrupt\n");
        break;
    case IRQ_M_SOFT:
        cprintf("Machine software interrupt\n");
        break;
    case IRQ_U_TIMER:
        cprintf("User timer interrupt\n");
        break;
    case IRQ_S_TIMER:
        /* sbi_set_timer 设置的是一次性比较值，必须先重设下一次时钟事件，
         * 否则只会收到这一次时钟中断。 */
        clock_set_next_event();
        /* 每次时钟中断 ticks 加一，达到 TICK_NUM 的整数倍时输出一行统计 */
        if (++ticks % TICK_NUM == 0)
        {
            print_ticks();
            /* 累计输出 SHUTDOWN_AFTER 行后，通过 SBI 请求 M 态固件关机 */
            if (++num_prints == SHUTDOWN_AFTER)
            {
                sbi_shutdown();
            }
        }
        break;
    case IRQ_H_TIMER:
        cprintf("Hypervisor timer interrupt\n");
        break;
    case IRQ_M_TIMER:
        cprintf("Machine timer interrupt\n");
        break;
    case IRQ_U_EXT:
        cprintf("User external interrupt\n");
        break;
    case IRQ_S_EXT:
        cprintf("Supervisor external interrupt\n");
        break;
    case IRQ_H_EXT:
        cprintf("Hypervisor external interrupt\n");
        break;
    case IRQ_M_EXT:
        cprintf("Machine external interrupt\n");
        break;
    default:
        /* 无法识别的中断：输出编号，并打印完整现场作为兜底 */
        cprintf("Unknown interrupt: cause %d\n", (int)cause);
        print_trapframe(tf);
        break;
    }
}

/* exception_handler - 处理异常（scause 最高位为 0）
 *   - 非法指令、断点：按规定格式输出异常类型和发生地址，
 *     再把 epc 推进到下一条指令，避免 sret 后重复执行同一条指令陷入死循环；
 *   - 其他异常：本实验不要求恢复，打印完整 trapframe 兜底。
 * epc 的推进量由 trap_insn_len() 按指令编码判断，不统一加 4。 */
void exception_handler(struct trapframe *tf)
{
    switch (tf->cause)
    {
    case CAUSE_ILLEGAL_INSTRUCTION:
        cprintf("Exception type: Illegal instruction\n");
        cprintf("Illegal instruction at 0x%016lx\n", tf->epc);
        /* 跳过触发异常的非法指令 */
        tf->epc += trap_insn_len(tf->epc);
        break;
    case CAUSE_BREAKPOINT:
        cprintf("Exception type: Breakpoint\n");
        cprintf("Breakpoint at 0x%016lx\n", tf->epc);
        /* ebreak 为 4 字节，c.ebreak 为 2 字节，按实际长度跳过 */
        tf->epc += trap_insn_len(tf->epc);
        break;
    default:
        /* 未要求处理的异常（如访存错误、ecall 等）只打印现场 */
        print_trapframe(tf);
        break;
    }
}

/* trap_dispatch - 根据 scause 的最高位分派：负数（最高位为 1）为中断，否则为异常 */
static inline void trap_dispatch(struct trapframe *tf)
{
    if ((intptr_t)tf->cause < 0)
    {
        // interrupts
        interrupt_handler(tf);
    }
    else
    {
        // exceptions
        exception_handler(tf);
    }
}

/* *
 * trap - handles or dispatches an exception/interrupt. if and when trap()
 * returns,
 * the code in kern/trap/trapentry.S restores the old CPU state saved in the
 * trapframe and then uses the iret instruction to return from the exception.
 * */
void trap(struct trapframe *tf)
{
    // dispatch based on what type of trap occurred
    // 由 __alltraps 汇编入口调用，tf 指向刚保存好的 trapframe
    trap_dispatch(tf);
}
