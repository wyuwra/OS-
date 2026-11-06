#include <sbi.h>
#include <defs.h>

/*
 * SBI(Supervisor Binary Interface)是运行在 S 态的操作系统内核与
 * M 态固件(如 OpenSBI)之间的标准接口。内核通过 ecall 指令陷入 M 态,
 * 请求固件完成只有 M 态才能做的事情(设置时钟、访问串口、关机等)。
 *
 * 下面是 SBI 调用号(legacy 版本的扩展编号),调用时放入寄存器 a7(x17)。
 */
uint64_t SBI_SET_TIMER = 0;                 // 设置下一次时钟中断的触发时间
uint64_t SBI_CONSOLE_PUTCHAR = 1;           // 向控制台(串口)输出一个字符
uint64_t SBI_CONSOLE_GETCHAR = 2;           // 从控制台读取一个字符
uint64_t SBI_CLEAR_IPI = 3;                 // 清除核间中断(IPI)标志
uint64_t SBI_SEND_IPI = 4;                  // 向其他核发送核间中断
uint64_t SBI_REMOTE_FENCE_I = 5;            // 让远程核执行 fence.i(同步指令缓存)
uint64_t SBI_REMOTE_SFENCE_VMA = 6;         // 让远程核执行 sfence.vma(刷新 TLB)
uint64_t SBI_REMOTE_SFENCE_VMA_ASID = 7;    // 同上,但只针对指定 ASID
uint64_t SBI_SHUTDOWN = 8;                  // 关机

/*
 * sbi_call:发起一次 SBI 调用的通用封装。
 * 参数约定(RISC-V 调用 SBI 的寄存器约定):
 *   a7(x17) = sbi_type,SBI 调用号
 *   a0(x10) = arg0, a1(x11) = arg1, a2(x12) = arg2,调用参数
 *   返回值通过 a0(x10) 带回
 * 执行 ecall 后,CPU 从 S 态陷入 M 态,由固件处理,处理完再返回 S 态。
 */
uint64_t sbi_call(uint64_t sbi_type, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    uint64_t ret_val;
    __asm__ volatile (
        "mv x17, %[sbi_type]\n"     // a7 <- 调用号
        "mv x10, %[arg0]\n"         // a0 <- 参数0
        "mv x11, %[arg1]\n"         // a1 <- 参数1
        "mv x12, %[arg2]\n"         // a2 <- 参数2
        "ecall\n"                   // 陷入 M 态,请求固件服务
        "mv %[ret_val], x10"        // 把 a0 中的返回值取出
        : [ret_val] "=r" (ret_val)  // 输出操作数:返回值
        : [sbi_type] "r" (sbi_type), [arg0] "r" (arg0), [arg1] "r" (arg1), [arg2] "r" (arg2)  // 输入操作数
        : "memory"                  // 告诉编译器内存可能被修改,防止错误优化/重排
    );
    return ret_val;
}

// 通过 SBI 向控制台输出一个字符(cprintf 最终就是逐字符调用它)
void sbi_console_putchar(unsigned char ch) {
    sbi_call(SBI_CONSOLE_PUTCHAR, ch, 0, 0);
}

// 通过 SBI 设置下一次时钟中断:当 time 计数器 >= stime_value 时触发 S 态时钟中断。
// 这是一次性的比较值,所以每次时钟中断处理中都要重新设置。
void sbi_set_timer(unsigned long long stime_value) {
    sbi_call(SBI_SET_TIMER, stime_value, 0, 0);
}

// 通过 SBI 从控制台读取一个字符,没有输入时返回 -1
int sbi_console_getchar(void) {
    return sbi_call(SBI_CONSOLE_GETCHAR, 0, 0, 0);
}

// 通过 SBI 关机(QEMU 会随之退出)
void sbi_shutdown(void)
{
	sbi_call(SBI_SHUTDOWN, 0, 0, 0);
}
