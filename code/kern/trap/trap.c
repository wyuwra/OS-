#include <assert.h>
#include <clock.h>
#include <console.h>
#include <defs.h>
#include <kdebug.h>
#include <memlayout.h>
#include <mmu.h>
#include <riscv.h>
#include <stdio.h>
#include <trap.h>
#include <vmm.h>

#define TICK_NUM 100

static void print_ticks()
{
    cprintf("%d ticks\n", TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

/* idt_init - initialize IDT to each of the entry points in kern/trap/vectors.S
 */
void idt_init(void)
{
    extern void __alltraps(void);
    /* Set sscratch register to 0, indicating to exception vector that we are
     * presently executing in the kernel */
    write_csr(sscratch, 0);
    /* Set the exception vector address */
    write_csr(stvec, &__alltraps);
    /* Allow kernel to access user memory */
    set_csr(sstatus, SSTATUS_SUM);
}

void print_trapframe(struct trapframe *tf)
{
    cprintf("trapframe at %p\n", tf);
    print_regs(&tf->gpr);
    cprintf("  status   0x%08x\n", tf->status);
    cprintf("  epc      0x%08x\n", tf->epc);
    cprintf("  badvaddr 0x%08x\n", tf->badvaddr);
    cprintf("  cause    0x%08x\n", tf->cause);
}

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

extern struct mm_struct *check_mm_struct;

//lab3 function2 2412737

/* trap_insn_len - 计算 epc 处那条指令的字节长度
 * RISC-V 指令长度由编码的最低几位决定：
 *   - 最低两位不为 0b11：16 位压缩指令（C 扩展，例如 c.ebreak，2 字节）；
 *   - 本实验的 32 位指令最低两位为 0b11（例如 ebreak、.word 0x7b）。
 * 本函数用于当前内核可读地址上的 16/32 位指令。
 * 由于 C 扩展只保证 2 字节对齐，按 16 位读取低半字，避免越界或非对齐访问。
 * 不使用 tf->badvaddr(stval)：规范允许非法指令异常时 stval 为 0，不可靠。 */
static size_t trap_insn_len(uintptr_t epc) {
    uint16_t low = *(const uint16_t *)epc;
    return ((low & 0x3) == 0x3) ? 4 : 2;
}

/* interrupt_handler - 处理中断（scause 最高位为 1）
 * 先去掉 scause 的最高位得到中断编号，再按编号分类：
 *   - S 态时钟中断：重设下一次时钟事件、计数、按 TICK_NUM 输出；
 *   - 其他已知中断：只输出类型提示；
 *   - 无法识别的中断：输出编号并打印完整 trapframe 便于调试。 */
void interrupt_handler(struct trapframe *tf) {
    /* 左移一位再右移一位，清掉表示“中断”的最高位，得到中断编号 */
    intptr_t cause = (tf->cause << 1) >> 1;
    switch (cause) {
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
            /* Lab4 保持线程运行，沿用重设和计数，不启用 Lab3 的定时关机。 */
            /* 设置下一次到期时刻，持续产生时钟事件。 */
            clock_set_next_event();
            /* 每次时钟中断 ticks 加一，达到 TICK_NUM 的整数倍时输出一行统计 */
            if (++ticks % TICK_NUM == 0) {
                print_ticks();
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
void exception_handler(struct trapframe *tf) {
    switch (tf->cause) {
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



/* *
 * trap - handles or dispatches an exception/interrupt. if and when trap()
 * returns,
 * the code in kern/trap/trapentry.S restores the old CPU state saved in the
 * trapframe and then uses the iret instruction to return from the exception.
 * */
void trap(struct trapframe *tf)
{
    // dispatch based on what type of trap occurred
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
