#include <riscv.h>

/*
 * 陷阱入口汇编（S 态）。
 * 栈上布局与 trap.h 中的 struct trapframe 一一对应（共 36 个 REGBYTES 槽位）：
 *   [0]      x0（zero，占位保存）
 *   [1..31]  x1..x31 通用寄存器（其中 x2 即 sp 的保存位置在 [2]）
 *   [32]     sstatus
 *   [33]     sepc
 *   [34]     sbadaddr
 *   [35]     scause
 * sscratch 在入口处被当作“来源标记”使用：陷入前若来自用户态，sscratch 里是内核栈；
 * 进入内核后被清零，若嵌套陷入时读到 0，就知道异常来自内核态。
 */

    /* SAVE_ALL - 保存被中断现场到内核栈，构造 trapframe */
    .macro SAVE_ALL

    # 暂存原 sp 到 sscratch，腾出 sp 作为新的内核栈指针
    csrw sscratch, sp

    # 在栈上开辟 36 个寄存器大小的 trapframe 空间
    addi sp, sp, -36 * REGBYTES
    # save x registers
    STORE x0, 0*REGBYTES(sp)
    STORE x1, 1*REGBYTES(sp)
    STORE x3, 3*REGBYTES(sp)
    STORE x4, 4*REGBYTES(sp)
    STORE x5, 5*REGBYTES(sp)
    STORE x6, 6*REGBYTES(sp)
    STORE x7, 7*REGBYTES(sp)
    STORE x8, 8*REGBYTES(sp)
    STORE x9, 9*REGBYTES(sp)
    STORE x10, 10*REGBYTES(sp)
    STORE x11, 11*REGBYTES(sp)
    STORE x12, 12*REGBYTES(sp)
    STORE x13, 13*REGBYTES(sp)
    STORE x14, 14*REGBYTES(sp)
    STORE x15, 15*REGBYTES(sp)
    STORE x16, 16*REGBYTES(sp)
    STORE x17, 17*REGBYTES(sp)
    STORE x18, 18*REGBYTES(sp)
    STORE x19, 19*REGBYTES(sp)
    STORE x20, 20*REGBYTES(sp)
    STORE x21, 21*REGBYTES(sp)
    STORE x22, 22*REGBYTES(sp)
    STORE x23, 23*REGBYTES(sp)
    STORE x24, 24*REGBYTES(sp)
    STORE x25, 25*REGBYTES(sp)
    STORE x26, 26*REGBYTES(sp)
    STORE x27, 27*REGBYTES(sp)
    STORE x28, 28*REGBYTES(sp)
    STORE x29, 29*REGBYTES(sp)
    STORE x30, 30*REGBYTES(sp)
    STORE x31, 31*REGBYTES(sp)

    # get sr, epc, badvaddr, cause
    # Set sscratch register to 0, so that if a recursive exception
    # occurs, the exception vector knows it came from the kernel
    # 原子地读出 sscratch（即陷入前的 sp）并将其清零，
    # 读出的旧 sp 存入 s0，随后写入 trapframe 的 sp 槽位 [2]

    csrrw s0, sscratch, x0
    
    # sstatus：陷入前的中断使能/特权级状态（SPP、SPIE 等）
    # sepc：陷入发生的指令地址，sret 返回时从这里继续执行
    # sbadaddr：出错的访存地址（若适用）
    # scause：陷入原因，最高位区分中断(1)与异常(0)
    
    csrr s1, sstatus
    csrr s2, sepc
    csrr s3, sbadaddr
    csrr s4, scause

    # 将 sp、sstatus、sepc、sbadaddr、scause 写入 trapframe
    
    STORE s0, 2*REGBYTES(sp)
    STORE s1, 32*REGBYTES(sp)
    STORE s2, 33*REGBYTES(sp)
    STORE s3, 34*REGBYTES(sp)
    STORE s4, 35*REGBYTES(sp)
    .endm

    /* RESTORE_ALL - 按 trapframe 恢复现场，与 SAVE_ALL 顺序相反 */
    .macro RESTORE_ALL

    #lab3 function1 2413276
    # 先恢复 sstatus 与 sepc，sret 会依据它们返回到正确的特权级与指令位置
    LOAD s0, 32*REGBYTES(sp)
    LOAD s1, 33*REGBYTES(sp)
    csrw sstatus, s0
    csrw sepc, s1

    # 恢复通用寄存器（x0 为硬连线 0，无需恢复；sp 放在最后恢复）
    LOAD x1, 1*REGBYTES(sp)
    LOAD x3, 3*REGBYTES(sp)
    LOAD x4, 4*REGBYTES(sp)
    LOAD x5, 5*REGBYTES(sp)
    LOAD x6, 6*REGBYTES(sp)
    LOAD x7, 7*REGBYTES(sp)
    LOAD x8, 8*REGBYTES(sp)
    LOAD x9, 9*REGBYTES(sp)
    LOAD x10, 10*REGBYTES(sp)
    LOAD x11, 11*REGBYTES(sp)
    LOAD x12, 12*REGBYTES(sp)
    LOAD x13, 13*REGBYTES(sp)
    LOAD x14, 14*REGBYTES(sp)
    LOAD x15, 15*REGBYTES(sp)
    LOAD x16, 16*REGBYTES(sp)
    LOAD x17, 17*REGBYTES(sp)
    LOAD x18, 18*REGBYTES(sp)
    LOAD x19, 19*REGBYTES(sp)
    LOAD x20, 20*REGBYTES(sp)
    LOAD x21, 21*REGBYTES(sp)
    LOAD x22, 22*REGBYTES(sp)
    LOAD x23, 23*REGBYTES(sp)
    LOAD x24, 24*REGBYTES(sp)
    LOAD x25, 25*REGBYTES(sp)
    LOAD x26, 26*REGBYTES(sp)
    LOAD x27, 27*REGBYTES(sp)
    LOAD x28, 28*REGBYTES(sp)
    LOAD x29, 29*REGBYTES(sp)
    LOAD x30, 30*REGBYTES(sp)
    LOAD x31, 31*REGBYTES(sp)
    # 最后恢复 sp（x2），此后 sp 不再指向 trapframe
    LOAD x2, 2*REGBYTES(sp)

    .endm

    /* __alltraps - stvec 指向的统一入口，所有陷入（中断与异常）都从这里进入 */
    .globl __alltraps
    .align(2)
__alltraps:
    SAVE_ALL

    #lab3 function1 2413276
    # 按 RISC-V 调用约定，把 trapframe 指针（即当前 sp）作为第一个参数 a0 传给 trap()
    mv a0, sp
    jal trap
    # sp should be the same as before "jal trap"

    /* __trapret - 从 trap() 返回后恢复现场并执行 sret，回到被中断的代码 */
    .globl __trapret
__trapret:
    RESTORE_ALL
    # return from supervisor call
    # sret 恢复特权级（SPP）与中断使能（SPIE），并跳转到 sepc
    sret
