#include <unistd.h>
#include <proc.h>
#include <syscall.h>
#include <trap.h>
#include <stdio.h>
#include <pmm.h>
#include <vmm.h>
#include <error.h>
#include <assert.h>

//lab5 function1 2412737
/* 系统调用的内核实现层。
 * 约定（与 user/libs/syscall.c 一致）：a0 = 调用号，a1~a5 = 参数 arg[0]~arg[4]，
 * 返回值由 syscall() 写回 tf->gpr.a0，经 __trapret/sret 带回用户态。
 * 各 sys_* 只做“参数解包 + 用户指针合法性检查”，真正的进程/内存语义交给 do_*。 */

/* user_ptr_ok - 检查当前进程能否以给定方式访问用户区间 [addr, addr+len)。
 * user_mem_check 会同时检查地址落在用户空间且属于带相应权限的 vma。 */
static bool
user_ptr_ok(const void *addr, size_t len, bool write) {
    struct mm_struct *mm = current->mm;
    bool ok;
    lock_mm(mm);
    ok = user_mem_check(mm, (uintptr_t)addr, len, write);
    unlock_mm(mm);
    return ok;
}

/* sys_exit(error_code)：进程退出，do_exit 不会返回。 */
static int
sys_exit(uint64_t arg[]) {
    int error_code = (int)arg[0];
    return do_exit(error_code);
}

/* sys_fork()：复制当前进程。
 * 传入当前 trapframe（epc 已越过 ecall），子进程沿用父进程的用户栈指针；
 * clone_flags = 0 表示复制而非共享地址空间。父进程得到子 pid，子进程由 copy_thread 置 a0 = 0。 */
static int
sys_fork(uint64_t arg[]) {
    struct trapframe *tf = current->tf;
    uintptr_t stack = tf->gpr.sp;
    return do_fork(0, stack, tf);
}

/* sys_wait(pid, store)：等待子进程（pid = 0 表示任意子进程），退出码写入用户指针 store。
 * store 可以为 NULL；非 NULL 时必须是当前进程可写的用户地址，否则返回 -E_INVAL
 * （badarg 用例会传入 0xC0000000 这类非法地址）。 */
static int
sys_wait(uint64_t arg[]) {
    int pid = (int)arg[0];
    int *store = (int *)arg[1];
    if (store != NULL && !user_ptr_ok(store, sizeof(int), 1)) {
        return -E_INVAL;
    }
    return do_wait(pid, store);
}

/* sys_exec(name, len, binary, size)：用 binary 指向的 ELF 映像替换当前进程。
 * name 与 binary 都来自用户空间，先检查可读。 */
static int
sys_exec(uint64_t arg[]) {
    const char *name = (const char *)arg[0];
    size_t len = (size_t)arg[1];
    unsigned char *binary = (unsigned char *)arg[2];
    size_t size = (size_t)arg[3];
    if (!user_ptr_ok(name, len, 0) || !user_ptr_ok(binary, size, 0)) {
        return -E_INVAL;
    }
    return do_execve(name, len, binary, size);
}

/* sys_yield()：主动让出 CPU，实际调度发生在 trap() 返回用户态前。 */
static int
sys_yield(uint64_t arg[]) {
    return do_yield();
}

/* sys_kill(pid)：给目标进程置 PF_EXITING，由其下次返回用户态时退出。 */
static int
sys_kill(uint64_t arg[]) {
    int pid = (int)arg[0];
    return do_kill(pid);
}

/* sys_getpid()：返回当前进程 pid。 */
static int
sys_getpid(uint64_t arg[]) {
    return current->pid;
}

/* sys_putc(c)：向控制台输出一个字符，用户态 cprintf 最终逐字符落到这里。 */
static int
sys_putc(uint64_t arg[]) {
    int c = (int)arg[0];
    cputchar(c);
    return 0;
}

/* sys_pgdir()：查看页表。pmm.h 虽声明了 print_pgdir()，但 pmm.c 中没有实现，
 * 直接调用会链接失败；这里保持与原 ucore 相同，只返回 0。 */
static int
sys_pgdir(uint64_t arg[]) {
    // print_pgdir();
    return 0;
}

static int (*syscalls[])(uint64_t arg[]) = {
    [SYS_exit]              sys_exit,
    [SYS_fork]              sys_fork,
    [SYS_wait]              sys_wait,
    [SYS_exec]              sys_exec,
    [SYS_yield]             sys_yield,
    [SYS_kill]              sys_kill,
    [SYS_getpid]            sys_getpid,
    [SYS_putc]              sys_putc,
    [SYS_pgdir]             sys_pgdir,
};

#define NUM_SYSCALLS        ((sizeof(syscalls)) / (sizeof(syscalls[0])))

/* syscall - 系统调用分发入口，由 exception_handler 在 CAUSE_USER_ECALL 时调用。
 * 从 current->tf 取调用号与参数；调用号先按有符号数判断，避免负数或超大值越界访问分发表；
 * 分发表中未登记的编号（如 SYS_clone、SYS_sleep）同样视为非法。
 * 非法调用号来自用户程序，不应让整个内核 panic，因此打印现场后返回 -E_INVAL。 */
void
syscall(void) {
    assert(current != NULL && current->tf != NULL);
    struct trapframe *tf = current->tf;
    uint64_t arg[5];
    int64_t num = (int64_t)tf->gpr.a0;
    if (num >= 0 && num < (int64_t)NUM_SYSCALLS && syscalls[num] != NULL) {
        arg[0] = tf->gpr.a1;
        arg[1] = tf->gpr.a2;
        arg[2] = tf->gpr.a3;
        arg[3] = tf->gpr.a4;
        arg[4] = tf->gpr.a5;
        tf->gpr.a0 = syscalls[num](arg);
        return;
    }
    print_trapframe(tf);
    cprintf("undefined syscall %ld, pid = %d, name = %s.\n",
            (long)num, current->pid, current->name);
    tf->gpr.a0 = -E_INVAL;
}
