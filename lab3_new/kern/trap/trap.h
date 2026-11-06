#ifndef __KERN_TRAP_TRAP_H__
#define __KERN_TRAP_TRAP_H__

#include <defs.h>

struct pushregs {
    uintptr_t zero;  // Hard-wired zero
    uintptr_t ra;    // Return address
    uintptr_t sp;    // Stack pointer
    uintptr_t gp;    // Global pointer
    uintptr_t tp;    // Thread pointer
    uintptr_t t0;    // Temporary
    uintptr_t t1;    // Temporary
    uintptr_t t2;    // Temporary
    uintptr_t s0;    // Saved register/frame pointer
    uintptr_t s1;    // Saved register
    uintptr_t a0;    // Function argument/return value
    uintptr_t a1;    // Function argument/return value
    uintptr_t a2;    // Function argument
    uintptr_t a3;    // Function argument
    uintptr_t a4;    // Function argument
    uintptr_t a5;    // Function argument
    uintptr_t a6;    // Function argument
    uintptr_t a7;    // Function argument
    uintptr_t s2;    // Saved register
    uintptr_t s3;    // Saved register
    uintptr_t s4;    // Saved register
    uintptr_t s5;    // Saved register
    uintptr_t s6;    // Saved register
    uintptr_t s7;    // Saved register
    uintptr_t s8;    // Saved register
    uintptr_t s9;    // Saved register
    uintptr_t s10;   // Saved register
    uintptr_t s11;   // Saved register
    uintptr_t t3;    // Temporary
    uintptr_t t4;    // Temporary
    uintptr_t t5;    // Temporary
    uintptr_t t6;    // Temporary
};

/*
 * trapframe:中断/异常发生时保存的完整现场(上下文)。
 * 在 trapentry.S 的 __alltraps 中,由汇编代码把寄存器依次压栈,
 * 形成这个结构体,再把其地址(a0)传给 C 函数 trap(tf)。
 * 处理完毕后由 __trapret 从中恢复寄存器并执行 sret 返回。
 */
struct trapframe {
    struct pushregs gpr;   // 32 个通用寄存器(x0~x31)的值,中断发生时被保存
    uintptr_t status;      // sstatus 寄存器:保存trap时的状态(如 SPP 表示来自哪个特权级、SPIE 表示trap前的中断使能位)
    uintptr_t epc;         // sepc 寄存器:trap发生时的指令地址(异常指令/被打断处),sret 时返回到此处继续执行
    uintptr_t badvaddr;    // stval 寄存器:与trap相关的附加信息,如访存异常时的出错地址、非法指令异常时的指令内容
    uintptr_t cause;       // scause 寄存器:trap原因;最高位为1表示中断(interrupt),为0表示异常(exception),低位为具体编号
};

void trap(struct trapframe *tf);
void idt_init(void);
void print_trapframe(struct trapframe *tf);
void print_regs(struct pushregs* gpr);
bool trap_in_kernel(struct trapframe *tf);

#endif /* !__KERN_TRAP_TRAP_H__ */
