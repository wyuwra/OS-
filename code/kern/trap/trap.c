#include <defs.h>
#include <mmu.h>
#include <memlayout.h>
#include <clock.h>
#include <trap.h>
#include <riscv.h>
#include <stdio.h>
#include <assert.h>
#include <console.h>
#include <vmm.h>
#include <kdebug.h>
#include <unistd.h>
#include <syscall.h>
#include <error.h>
#include <sched.h>
#include <sync.h>
#include <sbi.h>

#define TICK_NUM 100

static void print_ticks()
{
    cprintf("%d ticks\n", TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

/* idt_init - initialize IDT to each of the entry points in kern/trap/vectors.S */
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

/* trap_in_kernel - test if trap happened in kernel */
bool trap_in_kernel(struct trapframe *tf)
{
    return (tf->status & SSTATUS_SPP) != 0;
}

void print_trapframe(struct trapframe *tf)
{
    cprintf("trapframe at %p\n", tf);
    // cprintf("trapframe at 0x%x\n", tf);
    print_regs(&tf->gpr);
    cprintf("  status   0x%08x\n", tf->status);
    cprintf("  epc      0x%08x\n", tf->epc);
    cprintf("  tval 0x%08x\n", tf->tval);
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
            /* 设置下一次到期时刻，持续产生时钟事件。 */
            clock_set_next_event();
            /* lab5：每 TICK_NUM 次时钟中断请求一次重新调度，由 trap() 在返回用户态前执行 schedule()。 */
            if (++ticks % TICK_NUM == 0) {
                assert(current);
                current->need_resched = 1;
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
 * lab3 部分：非法指令、断点只输出类型与地址，并按指令长度推进 epc，避免 sret 后死循环。
 * lab5 部分：CAUSE_USER_ECALL 分支把 epc 前移 4 字节后调用 syscall()；
 *           用户态访存异常终止当前进程，内核态访存异常 panic。
 *           骨架未提供 kernel_execve_ret，kernel_execve 采用“构造 trapframe 后跳 __trapret”
 *           方案，因此断点分支保持 lab3 行为，不承担 ebreak 系统调用路径。 */
void exception_handler(struct trapframe *tf) {
    switch (tf->cause) {
        case CAUSE_ILLEGAL_INSTRUCTION:
            cprintf("Exception type: Illegal instruction\n");
            cprintf("Illegal instruction at 0x%016lx\n", tf->epc);
            tf->epc += trap_insn_len(tf->epc);
            break;
        case CAUSE_BREAKPOINT:
            cprintf("Exception type: Breakpoint\n");
            cprintf("Breakpoint at 0x%016lx\n", tf->epc);
            tf->epc += trap_insn_len(tf->epc);
            break;
        //lab5 function1 2412737: case CAUSE_USER_ECALL
        case CAUSE_USER_ECALL:
            /* 用户态 ecall：sepc 指向 ecall 本身（固定 4 字节，无压缩形式）。
             * 必须在 syscall() 之前前移：sys_fork 会复制当前 trapframe，
             * 子进程也要从 ecall 的下一条指令继续执行；否则返回后会重复 ecall。 */
            tf->epc += 4;
            syscall();
            break;
        case CAUSE_FETCH_PAGE_FAULT:
        case CAUSE_LOAD_PAGE_FAULT:
        case CAUSE_STORE_PAGE_FAULT:
        case CAUSE_FETCH_ACCESS:
        case CAUSE_LOAD_ACCESS:
        case CAUSE_STORE_ACCESS:
            /* 本实验未实现按需调页：访存异常若来自用户态，说明用户程序访问了非法地址
             * （例如 faultread 读 0 地址、faultreadkernel 读内核地址），只终止该进程；
             * 置 PF_EXITING 后由 trap() 在返回用户态前统一调用 do_exit(-E_KILLED)。
             * 若来自内核态，属于内核错误，直接 panic，避免无限重入同一条指令。 */
            if (!trap_in_kernel(tf) && current != NULL) {
                cprintf("user memory fault: cause %d, addr 0x%016lx, epc 0x%016lx, pid %d, killed by kernel.\n",
                        (int)tf->cause, tf->tval, tf->epc, current->pid);
                current->flags |= PF_EXITING;
            } else {
                print_trapframe(tf);
                panic("unhandled kernel memory fault: cause %d, addr 0x%016lx\n",
                      (int)tf->cause, tf->tval);
            }
            break;
        default:
            print_trapframe(tf);
            break;
    }
}



/* trap_dispatch - 按 scause 最高位区分中断与异常，分别交给对应的处理函数。
 * scause 最高位为 1 是中断（时钟等），为 0 是异常（含用户态 ecall）。 */
static inline void trap_dispatch(struct trapframe *tf)
{
    if ((intptr_t)tf->cause < 0)
    {
        // 中断
        interrupt_handler(tf);
    }
    else
    {
        // 异常
        exception_handler(tf);
    }
}

/* trap - 所有trap的 C 语言总入口，由 trapentry.S 中 __alltraps 在 SAVE_ALL 之后调用，
 * 参数 tf 指向 SAVE_ALL 刚在内核栈上保存的 trapframe。
 * trap() 返回后，__trapret 会用 RESTORE_ALL 恢复 trapframe 中的现场，再 sret 返回。
 *
 * lab5 的关键点：
 *   1. current->tf 必须指向“本次陷入”的 trapframe。syscall()、sys_fork 等都通过
 *      current->tf 读写用户寄存器（调用号、参数、返回值 a0）。
 *   2. 处理期间可能发生嵌套陷入（内核态又触发异常），所以先保存旧的 tf，处理完再还原。
 *   3. 只有“来自用户态”的陷入，才在返回前检查 PF_EXITING 与 need_resched：
 *      内核态被打断时不能随意退出或切换进程，否则会破坏内核正在执行的临界操作。 */
void trap(struct trapframe *tf)
{
    // 启动早期还没有当前进程（current 为空），直接分发即可
    if (current == NULL)
    {
        trap_dispatch(tf);
    }
    else
    {
        // 保存旧的 tf，并让 current->tf 指向本次陷入的 trapframe
        struct trapframe *otf = current->tf;
        current->tf = tf;

        // 在分发前记录陷入来源：SPP=1 为内核态，SPP=0 为用户态
        bool in_kernel = trap_in_kernel(tf);

        // 处理中断或异常；用户态 ecall 会在这里进入 syscall()
        trap_dispatch(tf);

        // 还原嵌套前的 tf
        current->tf = otf;
        if (!in_kernel)
        {
            // 进程被 kill（do_kill 置位）或在处理中被标记退出（如用户态访存错误）：
            // 在返回用户态之前真正退出，不再回到用户程序
            if (current->flags & PF_EXITING)
            {
                do_exit(-E_KILLED);
            }
            // 时间片用完（时钟中断置位）或主动 yield：让出 CPU
            if (current->need_resched)
            {
                schedule();
            }
        }
    }
}
