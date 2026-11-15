//LAB4 function3 2412737
#include <riscv.h>

# void switch_to(struct context *from, struct context *to);
#
# 按 RISC-V 调用约定，a0 = from（旧线程的 context），a1 = to（新线程的 context）。
# switch_to 以普通函数调用的形式出现，caller-saved 寄存器已由调用方编译器负责，
# 因此这里只需保存/恢复 struct context 中的 14 个寄存器：ra、sp、s0~s11。
# 偏移量与 kern/process/proc.h 中 struct context 的字段顺序一一对应，
# 第 i 个字段位于 i * REGBYTES 处。
#
# 本函数只负责寄存器迁移，不修改 current、页表或中断状态，也不做调度决策。

    .text
    .globl switch_to
switch_to:
    # 保存旧线程的执行现场到 from（a0）
    STORE ra, 0*REGBYTES(a0)
    STORE sp, 1*REGBYTES(a0)
    STORE s0, 2*REGBYTES(a0)
    STORE s1, 3*REGBYTES(a0)
    STORE s2, 4*REGBYTES(a0)
    STORE s3, 5*REGBYTES(a0)
    STORE s4, 6*REGBYTES(a0)
    STORE s5, 7*REGBYTES(a0)
    STORE s6, 8*REGBYTES(a0)
    STORE s7, 9*REGBYTES(a0)
    STORE s8, 10*REGBYTES(a0)
    STORE s9, 11*REGBYTES(a0)
    STORE s10, 12*REGBYTES(a0)
    STORE s11, 13*REGBYTES(a0)

    # 从 to（a1）恢复新线程的执行现场
    LOAD ra, 0*REGBYTES(a1)
    LOAD sp, 1*REGBYTES(a1)
    LOAD s0, 2*REGBYTES(a1)
    LOAD s1, 3*REGBYTES(a1)
    LOAD s2, 4*REGBYTES(a1)
    LOAD s3, 5*REGBYTES(a1)
    LOAD s4, 6*REGBYTES(a1)
    LOAD s5, 7*REGBYTES(a1)
    LOAD s6, 8*REGBYTES(a1)
    LOAD s7, 9*REGBYTES(a1)
    LOAD s8, 10*REGBYTES(a1)
    LOAD s9, 11*REGBYTES(a1)
    LOAD s10, 12*REGBYTES(a1)
    LOAD s11, 13*REGBYTES(a1)

    # 跳转到新线程的 ra：
    #   - 已运行过的线程：回到它上次调用 switch_to 的下一条指令（proc_run 内）；
    #   - 首次运行的线程：ra = forkret（copy_thread 设置），
    #     经 forkret -> forkrets -> __trapret -> sret 进入 kernel_thread_entry。
    ret
