#include <intr.h>
#include <riscv.h>

/*
 * 中断总开关驱动（S 态）。
 * 通过 sstatus 寄存器的 SIE 位（Supervisor Interrupt Enable）统一控制
 * S 态中断的全局开关：SIE=1 允许响应中断，SIE=0 屏蔽所有 S 态中断。
 * 具体某一类中断（如时钟中断）还需要 sie 寄存器中对应位的使能配合。
 */

/* intr_enable - enable irq interrupt
 * 置位 sstatus.SIE，打开 S 态全局中断。 */
void intr_enable(void) { set_csr(sstatus, SSTATUS_SIE); }

/* intr_disable - disable irq interrupt
 * 清零 sstatus.SIE，关闭 S 态全局中断（常用于临界区保护）。 */
void intr_disable(void) { clear_csr(sstatus, SSTATUS_SIE); }
