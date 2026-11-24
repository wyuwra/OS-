#include <riscv.h>

    .altmacro
    .align 2
    .macro SAVE_ALL
    LOCAL _restore_kernel_sp
    LOCAL _save_context

    # sscratch 约定：CPU 在用户态运行时，它保存本进程的内核栈顶；
    #                CPU 在内核态运行时，它为 0。
    # 交换 sp 与 sscratch：
    #   - 来自用户态：sp 变为内核栈顶，sscratch 变为用户栈指针（暂存）；
    #   - 来自内核态：sp 变为 0，sscratch 变为原来的内核 sp（暂存）。
    csrrw sp, sscratch, sp
    # sp 非 0 说明来自用户态，已经换到内核栈，直接保存现场
    bnez sp, _save_context

_restore_kernel_sp:
    # sp 为 0 说明来自内核态：从 sscratch 取回刚才暂存的内核 sp，继续使用当前内核栈
    csrr sp, sscratch
_save_context:
    # 在栈上分配一个 trapframe（36 个槽，每槽 REGBYTES 字节）
    addi sp, sp, -36 * REGBYTES
    # 保存通用寄存器 x0~x31（槽 0~31）；x2(sp) 稍后单独保存到槽 2
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

    # 读取陷入时的控制状态寄存器：sstatus、sepc、stval、scause
    # （s0~s4 的原值已在上面保存，这里可以借用做中转）
    # 读出 sscratch 的同时把它清零：
    #   - s0 得到被打断程序的原始 sp（用户态为用户栈指针，内核态为内核 sp）；
    #   - sscratch 置 0，表示“现在在内核中”；若处理期间再次发生异常，
    #     入口代码据此知道来自内核态，不会误换栈。
    csrrw s0, sscratch, x0
    csrr s1, sstatus
    csrr s2, sepc
    # 0x143 即 stval 寄存器的编号
    csrr s3, 0x143
    csrr s4, scause

    # 槽 2 保存原始 sp，槽 32~35 依次保存 sstatus、sepc、stval、scause
    STORE s0, 2*REGBYTES(sp)
    STORE s1, 32*REGBYTES(sp)
    STORE s2, 33*REGBYTES(sp)
    STORE s3, 34*REGBYTES(sp)
    STORE s4, 35*REGBYTES(sp)
    .endm

    .macro RESTORE_ALL
    LOCAL _save_kernel_sp
    LOCAL _restore_context

    #lab3 function1 2413276
    # 先恢复 sstatus 与 sepc，sret 会依据它们返回到正确的特权级与指令位置

    #lab5 function1 2412737
    # 双向返回：按保存下来的 sstatus.SPP 判断 sret 之后回到哪个特权级。
    #   SPP == 1：返回 S 态（内核被打断），sscratch 保持 SAVE_ALL 写入的 0，
    #             下次在内核里陷入时 SAVE_ALL 据此继续使用当前内核栈；
    #   SPP == 0：返回 U 态，把本进程内核栈顶（trapframe 之上，即 sp + 36*REGBYTES）
    #             写入 sscratch，下次从用户态陷入时 SAVE_ALL 才能换到内核栈。
    # 这里要求返回用户态的 trapframe 位于内核栈顶（用户陷入、copy_thread、
    # kernel_execve 构造的 trapframe 均满足）。s0~s2 稍后会被通用寄存器恢复覆盖。
    # s1 = 保存的 sstatus，s2 = 保存的 sepc
    LOAD s1, 32*REGBYTES(sp)
    LOAD s2, 33*REGBYTES(sp)

    # 取出 SPP 位：非 0（SPP=1）表示将返回内核态，跳过设置 sscratch
    andi s0, s1, SSTATUS_SPP
    bnez s0, _restore_context

_save_kernel_sp:
    # SPP=0，返回用户态：把内核栈顶（trapframe 之上）存入 sscratch，
    # 供下次从用户态陷入时 SAVE_ALL 换栈使用
    addi s0, sp, 36 * REGBYTES
    csrw sscratch, s0
_restore_context:
    # 恢复 sstatus 与 sepc，供 sret 使用
    csrw sstatus, s1
    csrw sepc, s2

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

    .globl __alltraps
__alltraps:
    SAVE_ALL

    #lab3 function1 2413276
    mv a0, sp
    jal trap
    # trap() 返回后，sp 仍指向同一个 trapframe（C 函数调用前后栈指针不变）

    .globl __trapret
__trapret:
    RESTORE_ALL
    # 从异常返回：由 sstatus.SPP 决定回到 S 态还是 U 态，pc 跳到 sepc
    sret

    .globl forkrets
forkrets:
    # 新进程首次运行：把栈指针指向它的 trapframe（a0 为参数），再走统一的返回路径
    move sp, a0
    j __trapret
