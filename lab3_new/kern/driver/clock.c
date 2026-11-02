#include <clock.h>
#include <defs.h>
#include <sbi.h>
#include <stdio.h>
#include <riscv.h>

/* 全局时钟节拍计数：每次时钟中断加一，volatile 保证中断处理与主流程读写可见 */
volatile size_t ticks;

/* get_cycles - 读取 RISC-V 的 time 计数器（rdtime 伪指令）
 * 64 位平台一条指令即可读出；32 位平台需要 rdtimeh/rdtime 反复读取并校验高位，
 * 防止低 32 位溢出进位导致读到不一致的值。 */
static inline uint64_t get_cycles(void) {
#if __riscv_xlen == 64
    uint64_t n;
    __asm__ __volatile__("rdtime %0" : "=r"(n));
    return n;
#else
    uint32_t lo, hi, tmp;
    __asm__ __volatile__(
        "1:\n"
        "rdtimeh %0\n"
        "rdtime %1\n"
        "rdtimeh %2\n"
        "bne %0, %2, 1b"
        : "=&r"(hi), "=&r"(lo), "=&r"(tmp));
    return ((uint64_t)hi << 32) | lo;
#endif
}

// Hardcode timebase
// 两次时钟中断之间的间隔（以 time 计数器的节拍为单位）。
// 在 QEMU 10MHz 下 100000 节拍 = 10ms，即每秒 100 次中断。
static uint64_t timebase = 100000;

/* *
 * clock_init - initialize 8253 clock to interrupt 100 times per second,
 * and then enable IRQ_TIMER.
 * 初始化时钟：在 sie 中开启 S 态定时器中断使能（STIP），
 * 通过 SBI 设置第一次超时时刻，并把 ticks 清零。
 * */
void clock_init(void) {
    // enable timer interrupt in sie
    // 在 sie 寄存器中打开 S 态定时器中断使能位（注意：全局开关 sstatus.SIE 另行控制）
    set_csr(sie, MIP_STIP);
    // divided by 500 when using Spike(2MHz)
    // divided by 100 when using QEMU(10MHz)
    // timebase = sbi_timebase() / 500;
    clock_set_next_event();

    // initialize time counter 'ticks' to zero
    ticks = 0;

    cprintf("++ setup timer interrupts\n");
}

/* clock_set_next_event - 设置“下一次”时钟中断的触发时刻（一次性，不会自动重复）
 * 以“当前时间 + timebase”作为新的比较值，通过 SBI 调用 sbi_set_timer
 * 交给 M 态固件（OpenSBI）记录。time 到达该时刻时只触发这一次 S 态定时器中断；
 * 要继续产生中断，必须在中断处理函数里再次调用本函数。 */
void clock_set_next_event(void) { sbi_set_timer(get_cycles() + timebase); }
